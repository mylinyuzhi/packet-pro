/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class hello_Wrapper */

#ifndef _Included_hello_Wrapper
#define _Included_hello_Wrapper
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     hello_Wrapper
 * Method:    eal_init
 * Signature: (I[Ljava/lang/String;)I
 */
JNIEXPORT jint JNICALL Java_hello_Wrapper_eal_1init
  (JNIEnv *, jclass, jint, jobjectArray);

/*
 * Class:     hello_Wrapper
 * Method:    lcore_id
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_hello_Wrapper_lcore_1id
  (JNIEnv *, jclass);

/*
 * Class:     hello_Wrapper
 * Method:    mp_wait_lcore
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_hello_Wrapper_mp_1wait_1lcore
  (JNIEnv *, jclass);

#ifdef __cplusplus
}
#endif
#endif
