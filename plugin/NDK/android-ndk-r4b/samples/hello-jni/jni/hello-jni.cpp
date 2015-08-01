
#include <string.h>
#include <jni.h>

static jint Java_com_example_hellojni_HelloJni_stringFromJNI( JNIEnv* env,
                                                  jobject thiz ){
	return (jint)42;
}
