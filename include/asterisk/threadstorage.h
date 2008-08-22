/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2006, Digium, Inc.
 *
 * Russell Bryant <russell@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file threadstorage.h
 * \author Russell Bryant <russell@digium.com>
 * \brief Definitions to aid in the use of thread local storage
 *
 * \arg \ref AstThreadStorage
 */

/*!
 * \page AstThreadStorage The Asterisk Thread Storage API
 *
 *
 * The POSIX threads (pthreads) API provides the ability to define thread
 * specific data.  The functions and structures defined here are intended
 * to centralize the code that is commonly used when using thread local
 * storage.
 *
 * The motivation for using this code in Asterisk is for situations where
 * storing data on a thread-specific basis can provide some amount of
 * performance benefit.  For example, there are some call types in Asterisk
 * where ast_frame structures must be allocated very rapidly (easily 50, 100,
 * 200 times a second).  Instead of doing the equivalent of that many calls
 * to malloc() and free() per second, thread local storage is used to keep a
 * list of unused frame structures so that they can be continuously reused.
 *
 * - \ref threadstorage.h
 */

#ifndef ASTERISK_THREADSTORAGE_H
#define ASTERISK_THREADSTORAGE_H

#include "asterisk/utils.h"
#include "asterisk/inline_api.h"

/*!
 * \brief data for a thread locally stored variable
 */
struct ast_threadstorage {
	pthread_once_t once;	/*!< Ensure that the key is only initialized by one thread */
	pthread_key_t key;	/*!< The key used to retrieve this thread's data */
	void (*key_init)(void);	/*!< The function that initializes the key */
	int (*custom_init)(void *); /*!< Custom initialization function specific to the object */
};

#ifdef SOLARIS
#define THREADSTORAGE_ONCE_INIT {PTHREAD_ONCE_INIT}
#else
#define THREADSTORAGE_ONCE_INIT PTHREAD_ONCE_INIT
#endif

#if defined(DEBUG_THREADLOCALS)
void __ast_threadstorage_object_add(void *key, size_t len, const char *file, const char *function, unsigned int line);
void __ast_threadstorage_object_remove(void *key);
void __ast_threadstorage_object_replace(void *key_old, void *key_new, size_t len);
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief Define a thread storage variable
 *
 * \param name The name of the thread storage object
 *
 * This macro would be used to declare an instance of thread storage in a file.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_buf);
 * \endcode
 */
#define AST_THREADSTORAGE(name) \
	AST_THREADSTORAGE_CUSTOM(name, NULL, ast_free_ptr) 

/*!
 * \brief Define a thread storage variable, with custom initialization and cleanup
 *
 * \param name The name of the thread storage object
 * \param init This is a custom function that will be called after each thread specific
 *           object is allocated, with the allocated block of memory passed
 *           as the argument.
 * \param cleanup This is a custom function that will be called instead of ast_free
 *              when the thread goes away.  Note that if this is used, it *MUST*
 *              call free on the allocated memory.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE_CUSTOM(my_buf, my_init, my_cleanup);
 * \endcode
 */
#if !defined(DEBUG_THREADLOCALS)
#define AST_THREADSTORAGE_CUSTOM(name, c_init, c_cleanup)	\
static void __init_##name(void);				\
static struct ast_threadstorage name = {			\
	.once = THREADSTORAGE_ONCE_INIT,			\
	.key_init = __init_##name,				\
	.custom_init = c_init,					\
};								\
static void __init_##name(void)					\
{								\
	pthread_key_create(&(name).key, c_cleanup);		\
}
#else /* defined(DEBUG_THREADLOCALS) */
#define AST_THREADSTORAGE_CUSTOM(name, c_init, c_cleanup)	\
static void __init_##name(void);				\
static struct ast_threadstorage name = {			\
	.once = THREADSTORAGE_ONCE_INIT,			\
	.key_init = __init_##name,				\
	.custom_init = c_init,					\
};								\
static void __cleanup_##name(void *data)			\
{								\
	__ast_threadstorage_object_remove(data);		\
	c_cleanup(data);					\
}								\
static void __init_##name(void)					\
{								\
	pthread_key_create(&(name).key, __cleanup_##name);	\
}
#endif /* defined(DEBUG_THREADLOCALS) */

/*!
 * \brief Retrieve thread storage
 *
 * \param ts This is a pointer to the thread storage structure declared by using
 *      the AST_THREADSTORAGE macro.  If declared with 
 *      AST_THREADSTORAGE(my_buf, my_buf_init), then this argument would be 
 *      (&my_buf).
 * \param init_size This is the amount of space to be allocated the first time
 *      this thread requests its data. Thus, this should be the size that the
 *      code accessing this thread storage is assuming the size to be.
 *
 * \return This function will return the thread local storage associated with
 *         the thread storage management variable passed as the first argument.
 *         The result will be NULL in the case of a memory allocation error.
 *
 * Example usage:
 * \code
 * AST_THREADSTORAGE(my_buf, my_buf_init);
 * #define MY_BUF_SIZE   128
 * ...
 * void my_func(const char *fmt, ...)
 * {
 *      void *buf;
 *
 *      if (!(buf = ast_threadstorage_get(&my_buf, MY_BUF_SIZE)))
 *           return;
 *      ...
 * }
 * \endcode
 */
#if !defined(DEBUG_THREADLOCALS)
AST_INLINE_API(
void *ast_threadstorage_get(struct ast_threadstorage *ts, size_t init_size),
{
	void *buf;

	pthread_once(&ts->once, ts->key_init);
	if (!(buf = pthread_getspecific(ts->key))) {
		if (!(buf = ast_calloc(1, init_size)))
			return NULL;
		if (ts->custom_init && ts->custom_init(buf)) {
			free(buf);
			return NULL;
		}
		pthread_setspecific(ts->key, buf);
	}

	return buf;
}
)
#else /* defined(DEBUG_THREADLOCALS) */
AST_INLINE_API(
void *__ast_threadstorage_get(struct ast_threadstorage *ts, size_t init_size, const char *file, const char *function, unsigned int line),
{
	void *buf;

	pthread_once(&ts->once, ts->key_init);
	if (!(buf = pthread_getspecific(ts->key))) {
		if (!(buf = ast_calloc(1, init_size)))
			return NULL;
		if (ts->custom_init && ts->custom_init(buf)) {
			free(buf);
			return NULL;
		}
		pthread_setspecific(ts->key, buf);
		__ast_threadstorage_object_add(&ts->key, init_size, file, function, line);
	}

	return buf;
}
)

#define ast_threadstorage_get(ts, init_size) __ast_threadstorage_get(ts, init_size, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif /* defined(DEBUG_THREADLOCALS) */

#endif /* ASTERISK_THREADSTORAGE_H */
