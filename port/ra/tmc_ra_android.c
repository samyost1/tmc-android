#include "tmc_ra_android.h"

#include "tmc_ra_android_queue.h"
#include "tmc_ra_badge_cache.h"
#include "tmc_ra_badge_gate.h"
#include "tmc_ra_runtime.h"
#include "port_repro.h"

#include <limits.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define TMC_RA_BADGE_SYNC_MAX 16u

typedef struct {
    JavaVM* vm;
    jobject bridge;
    jmethodID begin_request;
    jmethodID request_login;
    jmethodID sync_badges;
    jmethodID shutdown;
    jmethodID secret_set;
    jmethodID secret_delete;
    jmethodID secure_blob_get;
    jmethodID secure_blob_set;
    jmethodID secure_blob_delete;
    TmcRaAndroidQueue queue;
    pthread_mutex_t mutex;
    pthread_mutex_t lifecycle_mutex;
    bool queue_initialized;
    uint64_t badge_snapshot_generation;
} TmcRaAndroidState;

static TmcRaAndroidState sState = {
    .mutex = PTHREAD_MUTEX_INITIALIZER,
    .lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER,
};

static JNIEnv* GetEnv(void) {
    JNIEnv* env = NULL;

    if (sState.vm == NULL)
        return NULL;
    if ((*sState.vm)->GetEnv(sState.vm, (void**)&env, JNI_VERSION_1_6) == JNI_OK)
        return env;
    if ((*sState.vm)->AttachCurrentThread(sState.vm, &env, NULL) != JNI_OK)
        return NULL;
    return env;
}

static void ClearException(JNIEnv* env) {
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
}

static void Wipe(void* data, size_t size) {
    volatile uint8_t* bytes = data;

    while (size-- != 0)
        *bytes++ = 0;
}

static void WipeJavaByteArray(JNIEnv* env, jbyteArray array) {
    jbyte* bytes;
    jsize size;

    if (env == NULL || array == NULL)
        return;
    size = (*env)->GetArrayLength(env, array);
    if (size <= 0) {
        ClearException(env);
        return;
    }
    bytes = (*env)->GetByteArrayElements(env, array, NULL);
    if (bytes == NULL) {
        ClearException(env);
        return;
    }
    Wipe(bytes, (size_t)size);
    (*env)->ReleaseByteArrayElements(env, array, bytes, 0);
    ClearException(env);
}

static bool CopyJavaBytes(JNIEnv* env, jbyteArray source, uint8_t* destination,
                          size_t capacity, size_t* size) {
    jsize length;

    if (size == NULL)
        return false;
    *size = 0;
    if (env == NULL || source == NULL || destination == NULL)
        return false;
    length = (*env)->GetArrayLength(env, source);
    if (length <= 0 || (size_t)length > capacity)
        return false;
    (*env)->GetByteArrayRegion(env, source, 0, length, (jbyte*)destination);
    if ((*env)->ExceptionCheck(env)) {
        ClearException(env);
        return false;
    }
    if (memchr(destination, 0, (size_t)length) != NULL)
        return false;
    *size = (size_t)length;
    return true;
}

static uint64_t Now(void* userdata) {
    (void)userdata;
    return TmcRaRuntime_MonotonicMs();
}

static void Begin(void* userdata, NRA_RequestId id, const NRA_HttpRequest* request) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env;
    jstring url;
    jbyteArray post_data;
    jstring content_type;
    jstring user_agent;
    jobject bridge;
    jmethodID begin_request;
    uint64_t generation;
    size_t post_size;

    if (state == NULL || request == NULL)
        return;
    env = GetEnv();
    if (env == NULL)
        return;
    pthread_mutex_lock(&state->lifecycle_mutex);
    pthread_mutex_lock(&state->mutex);
    if (state->bridge == NULL || state->begin_request == NULL || !state->queue_initialized) {
        pthread_mutex_unlock(&state->mutex);
        pthread_mutex_unlock(&state->lifecycle_mutex);
        return;
    }
    generation = TmcRaAndroidQueue_Generation(&state->queue);
    bridge = (*env)->NewLocalRef(env, state->bridge);
    begin_request = state->begin_request;
    pthread_mutex_unlock(&state->mutex);
    if (bridge == NULL) {
        pthread_mutex_unlock(&state->lifecycle_mutex);
        return;
    }
    post_size = request->post_data == NULL ? 0 : strlen(request->post_data);
    if (post_size > (size_t)INT_MAX) {
        (*env)->DeleteLocalRef(env, bridge);
        pthread_mutex_unlock(&state->lifecycle_mutex);
        return;
    }
    url = (*env)->NewStringUTF(env, request->url ? request->url : "");
    post_data = (*env)->NewByteArray(env, (jsize)post_size);
    content_type = (*env)->NewStringUTF(env, request->content_type ? request->content_type : "");
    user_agent = (*env)->NewStringUTF(env, request->user_agent ? request->user_agent : "");
    if (post_data != NULL && post_size != 0) {
        (*env)->SetByteArrayRegion(env, post_data, 0, (jsize)post_size, (const jbyte*)request->post_data);
    }
    if (!(*env)->ExceptionCheck(env) && url != NULL && post_data != NULL
        && content_type != NULL && user_agent != NULL) {
        (*env)->CallVoidMethod(env, bridge, begin_request, (jlong)generation, (jlong)id, url,
                               post_data, content_type, user_agent, (jint)request->connect_timeout_ms,
                               (jint)request->read_timeout_ms, (jint)request->max_response_bytes);
        if ((*env)->ExceptionCheck(env)) {
            ClearException(env);
            WipeJavaByteArray(env, post_data);
        }
    } else {
        ClearException(env);
        WipeJavaByteArray(env, post_data);
    }
    if (url != NULL) (*env)->DeleteLocalRef(env, url);
    if (post_data != NULL) (*env)->DeleteLocalRef(env, post_data);
    if (content_type != NULL) (*env)->DeleteLocalRef(env, content_type);
    if (user_agent != NULL) (*env)->DeleteLocalRef(env, user_agent);
    (*env)->DeleteLocalRef(env, bridge);
    pthread_mutex_unlock(&state->lifecycle_mutex);
}

void TmcRaAndroid_RequestPasswordLogin(void) {
    JNIEnv* env;

    env = GetEnv();
    if (env == NULL)
        return;
    pthread_mutex_lock(&sState.mutex);
    if (sState.bridge != NULL && sState.request_login != NULL) {
        const uint64_t generation = TmcRaAndroidQueue_Generation(&sState.queue);
        (*env)->CallVoidMethod(env, sState.bridge, sState.request_login, (jlong)generation);
        ClearException(env);
    }
    pthread_mutex_unlock(&sState.mutex);
}

static void RequestLogin(void* userdata, const NRA_UICommand* command) {
    (void)userdata;
    if (command == NULL || command->kind != NRA_UI_COMMAND_REQUEST_PASSWORD_LOGIN)
        return;
    TmcRaAndroid_RequestPasswordLogin();
}

static bool AddBadgeUrl(const char* urls[], size_t* count, const char* url) {
    size_t i;

    if (url == NULL || url[0] == '\0' || *count >= TMC_RA_BADGE_SYNC_MAX)
        return false;
    for (i = 0; i < *count; ++i) {
        if (strcmp(urls[i], url) == 0)
            return false;
    }
    urls[(*count)++] = url;
    return true;
}

static bool BadgeIsCurrent(void* userdata, uint64_t generation) {
    TmcRaAndroidState* state = userdata;

    return state->bridge != NULL && state->queue_initialized &&
        TmcRaAndroidQueue_Generation(&state->queue) == generation;
}

static void SyncBadges(NRA_Context* context) {
    NRA_UISnapshot snapshot;
    const char* urls[TMC_RA_BADGE_SYNC_MAX];
    size_t url_count = 0;
    size_t i;
    JNIEnv* env;
    jobject bridge;
    jclass string_class;
    jobjectArray array;
    uint64_t generation;

    if (context == NULL || !nra_copy_ui_snapshot(context, &snapshot))
        return;
    for (i = 0; i < snapshot.achievement_count && i < 4; ++i)
        (void)AddBadgeUrl(urls, &url_count, snapshot.achievements[i].badge_url);
    for (i = 0; i < snapshot.toast_count; ++i)
        (void)AddBadgeUrl(urls, &url_count, snapshot.toasts[i].badge_url);

    env = GetEnv();
    if (env == NULL)
        return;
    pthread_mutex_lock(&sState.lifecycle_mutex);
    pthread_mutex_lock(&sState.mutex);
    if (!sState.queue_initialized || sState.bridge == NULL || sState.sync_badges == NULL ||
        sState.badge_snapshot_generation == snapshot.generation) {
        pthread_mutex_unlock(&sState.mutex);
        pthread_mutex_unlock(&sState.lifecycle_mutex);
        return;
    }
    sState.badge_snapshot_generation = snapshot.generation;
    bridge = (*env)->NewLocalRef(env, sState.bridge);
    generation = TmcRaAndroidQueue_Generation(&sState.queue);
    pthread_mutex_unlock(&sState.mutex);
    if (bridge == NULL) {
        pthread_mutex_unlock(&sState.lifecycle_mutex);
        return;
    }
    string_class = (*env)->FindClass(env, "java/lang/String");
    array = string_class != NULL
        ? (*env)->NewObjectArray(env, (jsize)url_count, string_class, NULL)
        : NULL;
    if (array != NULL) {
        for (i = 0; i < url_count; ++i) {
            jstring url = (*env)->NewStringUTF(env, urls[i]);
            if (url == NULL) {
                ClearException(env);
                continue;
            }
            (*env)->SetObjectArrayElement(env, array, (jsize)i, url);
            (*env)->DeleteLocalRef(env, url);
        }
        (*env)->CallVoidMethod(env, bridge, sState.sync_badges, (jlong)generation, array);
        ClearException(env);
        (*env)->DeleteLocalRef(env, array);
    } else {
        ClearException(env);
    }
    if (string_class != NULL)
        (*env)->DeleteLocalRef(env, string_class);
    (*env)->DeleteLocalRef(env, bridge);
    pthread_mutex_unlock(&sState.lifecycle_mutex);
}

static void Shutdown(void* userdata) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env;
    jobject bridge;
    jmethodID shutdown;

    if (state == NULL)
        return;
    pthread_mutex_lock(&state->lifecycle_mutex);
    env = GetEnv();
    pthread_mutex_lock(&state->mutex);
    if (state->queue_initialized)
        TmcRaAndroidQueue_Close(&state->queue);
    bridge = env != NULL && state->bridge != NULL ? (*env)->NewLocalRef(env, state->bridge) : NULL;
    shutdown = state->shutdown;
    pthread_mutex_unlock(&state->mutex);
    if (env != NULL && bridge != NULL && shutdown != NULL) {
        (*env)->CallVoidMethod(env, bridge, shutdown);
        ClearException(env);
    }
    pthread_mutex_lock(&state->mutex);
    if (env != NULL && state->bridge != NULL)
        (*env)->DeleteGlobalRef(env, state->bridge);
    state->bridge = NULL;
    state->begin_request = NULL;
    state->request_login = NULL;
    state->sync_badges = NULL;
    state->shutdown = NULL;
    state->secret_set = NULL;
    state->secret_delete = NULL;
    state->secure_blob_get = NULL;
    state->secure_blob_set = NULL;
    state->secure_blob_delete = NULL;
    state->badge_snapshot_generation = 0;
    pthread_mutex_unlock(&state->mutex);
    if (bridge != NULL)
        (*env)->DeleteLocalRef(env, bridge);
    TmcRaBadgeCache_Reset();
    pthread_mutex_unlock(&state->lifecycle_mutex);
}

static NRA_Result SecretSet(void* userdata, const char* key, const uint8_t* value, size_t size) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env = GetEnv();
    jstring name;
    jbyteArray bytes;
    jboolean stored = JNI_FALSE;

    if (state == NULL || env == NULL || key == NULL || value == NULL || size == 0 || size > TMC_RA_ANDROID_MAX_TOKEN_BYTES)
        return NRA_INVALID_ARGUMENT;
    pthread_mutex_lock(&state->mutex);
    if (state->bridge == NULL || state->secret_set == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return NRA_DISABLED;
    }
    name = (*env)->NewStringUTF(env, key);
    bytes = (*env)->NewByteArray(env, (jsize)size);
    if (name != NULL && bytes != NULL) {
        (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)size, (const jbyte*)value);
        stored = (*env)->CallBooleanMethod(env, state->bridge, state->secret_set, name, bytes);
        ClearException(env);
        WipeJavaByteArray(env, bytes);
    }
    if (name != NULL) (*env)->DeleteLocalRef(env, name);
    if (bytes != NULL) (*env)->DeleteLocalRef(env, bytes);
    pthread_mutex_unlock(&state->mutex);
    return stored == JNI_TRUE ? NRA_OK : NRA_IO_ERROR;
}

static NRA_Result SecretDelete(void* userdata, const char* key) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env = GetEnv();
    jstring name;

    if (state == NULL || env == NULL || key == NULL)
        return NRA_INVALID_ARGUMENT;
    pthread_mutex_lock(&state->mutex);
    if (state->bridge == NULL || state->secret_delete == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return NRA_DISABLED;
    }
    name = (*env)->NewStringUTF(env, key);
    if (name != NULL) {
        (*env)->CallVoidMethod(env, state->bridge, state->secret_delete, name);
        ClearException(env);
        (*env)->DeleteLocalRef(env, name);
    }
    pthread_mutex_unlock(&state->mutex);
    return NRA_OK;
}

static NRA_Result SecureBlobGet(void* userdata, const char* key, uint8_t* value, size_t capacity, size_t* size) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env = GetEnv();
    jstring name;
    jbyteArray bytes;
    jsize length;
    NRA_Result result = NRA_IO_ERROR;

    if (size == NULL)
        return NRA_INVALID_ARGUMENT;
    *size = 0;
    if (state == NULL || env == NULL || key == NULL)
        return NRA_INVALID_ARGUMENT;
    pthread_mutex_lock(&state->mutex);
    if (state->bridge == NULL || state->secure_blob_get == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return NRA_DISABLED;
    }
    name = (*env)->NewStringUTF(env, key);
    bytes = name != NULL ? (jbyteArray)(*env)->CallObjectMethod(env, state->bridge, state->secure_blob_get, name) : NULL;
    ClearException(env);
    if (bytes == NULL) {
        result = NRA_IO_ERROR;
        goto done;
    }
    length = (*env)->GetArrayLength(env, bytes);
    if (length == 0) {
        result = NRA_OK;
        goto wipe;
    }
    if (length < 0 || (size_t)length > TMC_RA_ANDROID_MAX_SECURE_BLOB_BYTES ||
        value == NULL || capacity < (size_t)length) {
        result = NRA_INVALID_ARGUMENT;
        goto wipe;
    }
    (*env)->GetByteArrayRegion(env, bytes, 0, length, (jbyte*)value);
    if ((*env)->ExceptionCheck(env)) {
        ClearException(env);
        goto wipe;
    }
    *size = (size_t)length;
    result = NRA_OK;

wipe:
    WipeJavaByteArray(env, bytes);
    (*env)->DeleteLocalRef(env, bytes);
done:
    if (name != NULL)
        (*env)->DeleteLocalRef(env, name);
    pthread_mutex_unlock(&state->mutex);
    return result;
}

static NRA_Result SecureBlobSet(void* userdata, const char* key, const uint8_t* value, size_t size) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env = GetEnv();
    jstring name;
    jbyteArray bytes;
    jboolean stored = JNI_FALSE;

    if (state == NULL || env == NULL || key == NULL || value == NULL || size == 0 ||
        size > TMC_RA_ANDROID_MAX_SECURE_BLOB_BYTES)
        return NRA_INVALID_ARGUMENT;
    pthread_mutex_lock(&state->mutex);
    if (state->bridge == NULL || state->secure_blob_set == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return NRA_DISABLED;
    }
    name = (*env)->NewStringUTF(env, key);
    bytes = name != NULL ? (*env)->NewByteArray(env, (jsize)size) : NULL;
    if (bytes != NULL) {
        (*env)->SetByteArrayRegion(env, bytes, 0, (jsize)size, (const jbyte*)value);
        stored = (*env)->CallBooleanMethod(env, state->bridge, state->secure_blob_set, name, bytes);
        ClearException(env);
        WipeJavaByteArray(env, bytes);
    }
    if (name != NULL)
        (*env)->DeleteLocalRef(env, name);
    if (bytes != NULL)
        (*env)->DeleteLocalRef(env, bytes);
    pthread_mutex_unlock(&state->mutex);
    return stored == JNI_TRUE ? NRA_OK : NRA_IO_ERROR;
}

static NRA_Result SecureBlobDelete(void* userdata, const char* key) {
    TmcRaAndroidState* state = userdata;
    JNIEnv* env = GetEnv();
    jstring name;
    jboolean deleted = JNI_FALSE;

    if (state == NULL || env == NULL || key == NULL)
        return NRA_INVALID_ARGUMENT;
    pthread_mutex_lock(&state->mutex);
    if (state->bridge == NULL || state->secure_blob_delete == NULL) {
        pthread_mutex_unlock(&state->mutex);
        return NRA_DISABLED;
    }
    name = (*env)->NewStringUTF(env, key);
    if (name != NULL) {
        deleted = (*env)->CallBooleanMethod(env, state->bridge, state->secure_blob_delete, name);
        ClearException(env);
        (*env)->DeleteLocalRef(env, name);
    }
    pthread_mutex_unlock(&state->mutex);
    return deleted == JNI_TRUE ? NRA_OK : NRA_IO_ERROR;
}

bool TmcRaAndroid_IsRegistered(void) {
    bool registered;

    pthread_mutex_lock(&sState.mutex);
    registered = sState.bridge != NULL;
    pthread_mutex_unlock(&sState.mutex);
    return registered;
}

const NRA_PlatformVTable* TmcRaAndroid_Platform(void) {
    static const NRA_PlatformVTable platform = {
        .now_ms = Now,
        .http_begin = Begin,
        .http_shutdown = Shutdown,
        .secret_set = SecretSet,
        .secret_delete = SecretDelete,
        .secure_blob_get = SecureBlobGet,
        .secure_blob_set = SecureBlobSet,
        .secure_blob_delete = SecureBlobDelete,
        .post_ui_command = RequestLogin,
    };
    return &platform;
}

void* TmcRaAndroid_PlatformUserdata(void) {
    return &sState;
}

void TmcRaAndroid_Drain(NRA_Context* context) {
    TmcRaAndroidCompletion completion;
    TmcRaAndroidLogin login;
    uint64_t generation;

    if (context == NULL)
        return;
    pthread_mutex_lock(&sState.mutex);
    if (!sState.queue_initialized) {
        pthread_mutex_unlock(&sState.mutex);
        return;
    }
    generation = TmcRaAndroidQueue_Generation(&sState.queue);
    pthread_mutex_unlock(&sState.mutex);
    while (TmcRaAndroidQueue_TakeCompletion(&sState.queue, generation, &completion)) {
        (void)nra_enqueue_http_completion(context, &completion.completion);
        TmcRaAndroidQueue_ReleaseCompletion(&completion);
    }
    if (TmcRaAndroidQueue_TakePassword(&sState.queue, generation, &login)) {
        if (login.token_login)
            (void)nra_login_token(context, login.username, login.token);
        else
            (void)nra_login_password(context, login.username, login.password);
        TmcRaAndroidQueue_WipeLogin(&login);
    }
    SyncBadges(context);
}

JNIEXPORT jboolean JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeAttach(
    JNIEnv* env, jclass clazz, jobject bridge) {
    jclass bridge_class;

    (void)clazz;
    if (bridge == NULL || (*env)->GetJavaVM(env, &sState.vm) != JNI_OK)
        return JNI_FALSE;
    pthread_mutex_lock(&sState.lifecycle_mutex);
    pthread_mutex_lock(&sState.mutex);
    if (!sState.queue_initialized) {
        if (!TmcRaAndroidQueue_Init(&sState.queue)) {
            pthread_mutex_unlock(&sState.mutex);
            pthread_mutex_unlock(&sState.lifecycle_mutex);
            return JNI_FALSE;
        }
        sState.queue_initialized = true;
    }
    if (sState.bridge != NULL) {
        pthread_mutex_unlock(&sState.mutex);
        pthread_mutex_unlock(&sState.lifecycle_mutex);
        return JNI_FALSE;
    }
    TmcRaAndroidQueue_Reopen(&sState.queue);
    bridge_class = (*env)->GetObjectClass(env, bridge);
    if (bridge_class == NULL) {
        ClearException(env);
        pthread_mutex_unlock(&sState.mutex);
        pthread_mutex_unlock(&sState.lifecycle_mutex);
        return JNI_FALSE;
    }
    sState.begin_request = (*env)->GetMethodID(env, bridge_class, "beginRequest",
                                                "(JJLjava/lang/String;[BLjava/lang/String;Ljava/lang/String;III)V");
    sState.request_login = (*env)->GetMethodID(env, bridge_class, "requestPasswordLogin", "(J)V");
    sState.sync_badges = (*env)->GetMethodID(env, bridge_class, "syncBadgeImages",
                                             "(J[Ljava/lang/String;)V");
    sState.shutdown = (*env)->GetMethodID(env, bridge_class, "shutdownFromNative", "()V");
    sState.secret_set = (*env)->GetMethodID(env, bridge_class, "saveCredentialPart",
                                            "(Ljava/lang/String;[B)Z");
    sState.secret_delete = (*env)->GetMethodID(env, bridge_class, "deleteCredentials",
                                               "(Ljava/lang/String;)V");
    sState.secure_blob_get = (*env)->GetMethodID(env, bridge_class, "loadSecureBlob",
                                                  "(Ljava/lang/String;)[B");
    sState.secure_blob_set = (*env)->GetMethodID(env, bridge_class, "saveSecureBlob",
                                                  "(Ljava/lang/String;[B)Z");
    sState.secure_blob_delete = (*env)->GetMethodID(env, bridge_class, "deleteSecureBlob",
                                                     "(Ljava/lang/String;)Z");
    (*env)->DeleteLocalRef(env, bridge_class);
    if (sState.begin_request == NULL || sState.request_login == NULL || sState.sync_badges == NULL ||
        sState.shutdown == NULL
        || sState.secret_set == NULL || sState.secret_delete == NULL
        || sState.secure_blob_get == NULL || sState.secure_blob_set == NULL ||
        sState.secure_blob_delete == NULL) {
        ClearException(env);
        sState.begin_request = NULL;
        sState.request_login = NULL;
        sState.sync_badges = NULL;
        sState.shutdown = NULL;
        sState.secret_set = NULL;
        sState.secret_delete = NULL;
        sState.secure_blob_get = NULL;
        sState.secure_blob_set = NULL;
        sState.secure_blob_delete = NULL;
        pthread_mutex_unlock(&sState.mutex);
        pthread_mutex_unlock(&sState.lifecycle_mutex);
        return JNI_FALSE;
    }
    sState.badge_snapshot_generation = 0;
    sState.bridge = (*env)->NewGlobalRef(env, bridge);
    const bool attached = sState.bridge != NULL;
    pthread_mutex_unlock(&sState.mutex);
    pthread_mutex_unlock(&sState.lifecycle_mutex);
    return attached ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeDetach(
    JNIEnv* env, jclass clazz) {
    jobject bridge;
    jmethodID shutdown;

    (void)clazz;
    pthread_mutex_lock(&sState.lifecycle_mutex);
    pthread_mutex_lock(&sState.mutex);
    if (sState.queue_initialized)
        TmcRaAndroidQueue_Close(&sState.queue);
    bridge = sState.bridge != NULL ? (*env)->NewLocalRef(env, sState.bridge) : NULL;
    shutdown = sState.shutdown;
    pthread_mutex_unlock(&sState.mutex);
    if (bridge != NULL && shutdown != NULL) {
        (*env)->CallVoidMethod(env, bridge, shutdown);
        ClearException(env);
    }
    pthread_mutex_lock(&sState.mutex);
    if (sState.bridge != NULL)
        (*env)->DeleteGlobalRef(env, sState.bridge);
    sState.bridge = NULL;
    sState.begin_request = NULL;
    sState.request_login = NULL;
    sState.sync_badges = NULL;
    sState.shutdown = NULL;
    sState.secret_set = NULL;
    sState.secret_delete = NULL;
    sState.secure_blob_get = NULL;
    sState.secure_blob_set = NULL;
    sState.secure_blob_delete = NULL;
    sState.badge_snapshot_generation = 0;
    pthread_mutex_unlock(&sState.mutex);
    if (bridge != NULL)
        (*env)->DeleteLocalRef(env, bridge);
    TmcRaBadgeCache_Reset();
    pthread_mutex_unlock(&sState.lifecycle_mutex);
}

JNIEXPORT jboolean JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeRequestCapture(
    JNIEnv* env, jclass clazz, jstring checkpoint, jstring output_path) {
    const char* checkpoint_chars;
    const char* output_path_chars;
    bool accepted;

    (void)clazz;
    if (checkpoint == NULL || output_path == NULL)
        return JNI_FALSE;
    checkpoint_chars = (*env)->GetStringUTFChars(env, checkpoint, NULL);
    if (checkpoint_chars == NULL) {
        ClearException(env);
        return JNI_FALSE;
    }
    output_path_chars = (*env)->GetStringUTFChars(env, output_path, NULL);
    if (output_path_chars == NULL) {
        (*env)->ReleaseStringUTFChars(env, checkpoint, checkpoint_chars);
        ClearException(env);
        return JNI_FALSE;
    }
    accepted = Port_ReproRaCapture_Request(checkpoint_chars, output_path_chars);
    (*env)->ReleaseStringUTFChars(env, output_path, output_path_chars);
    (*env)->ReleaseStringUTFChars(env, checkpoint, checkpoint_chars);
    ClearException(env);
    return accepted ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeHttpCompleted(
    JNIEnv* env, jclass clazz, jlong generation, jlong request_id, jint status, jbyteArray body, jboolean retryable) {
    NRA_HttpCompletion completion = {.request_id = (NRA_RequestId)request_id, .http_status_code = status,
                                     .retryable = retryable == JNI_TRUE};
    jsize body_size = body ? (*env)->GetArrayLength(env, body) : 0;
    uint8_t* buffer = NULL;

    (void)clazz;
    if (body_size < 0 || (uint32_t)body_size > TMC_RA_ANDROID_MAX_RESPONSE_BYTES)
        return;
    if (body_size != 0) {
        buffer = malloc((size_t)body_size);
        if (buffer == NULL)
            return;
        (*env)->GetByteArrayRegion(env, body, 0, body_size, (jbyte*)buffer);
        if ((*env)->ExceptionCheck(env)) {
            ClearException(env);
            Wipe(buffer, (size_t)body_size);
            free(buffer);
            return;
        }
        completion.body = buffer;
        completion.body_size = (size_t)body_size;
    }
    pthread_mutex_lock(&sState.mutex);
    if (sState.queue_initialized)
        (void)TmcRaAndroidQueue_EnqueueCompletion(&sState.queue, (uint64_t)generation, &completion);
    pthread_mutex_unlock(&sState.mutex);
    if (buffer != NULL) {
        Wipe(buffer, (size_t)body_size);
        free(buffer);
    }
}

JNIEXPORT void JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativePassword(
    JNIEnv* env, jclass clazz, jlong generation, jbyteArray username, jbyteArray password) {
    uint8_t user_bytes[TMC_RA_ANDROID_MAX_USERNAME_BYTES] = {0};
    uint8_t password_bytes[TMC_RA_ANDROID_MAX_PASSWORD_BYTES] = {0};
    size_t user_size = 0;
    size_t password_size = 0;

    (void)clazz;
    if (username == NULL || password == NULL) {
        WipeJavaByteArray(env, username);
        WipeJavaByteArray(env, password);
        return;
    }
    if (CopyJavaBytes(env, username, user_bytes, sizeof(user_bytes), &user_size)
        && CopyJavaBytes(env, password, password_bytes, sizeof(password_bytes), &password_size)) {
        pthread_mutex_lock(&sState.mutex);
        if (sState.queue_initialized)
            (void)TmcRaAndroidQueue_EnqueuePassword(&sState.queue, (uint64_t)generation,
                                                    user_bytes, user_size, password_bytes, password_size);
        pthread_mutex_unlock(&sState.mutex);
    }
    WipeJavaByteArray(env, username);
    WipeJavaByteArray(env, password);
    Wipe(user_bytes, sizeof(user_bytes));
    Wipe(password_bytes, sizeof(password_bytes));
}

JNIEXPORT void JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeToken(
    JNIEnv* env, jclass clazz, jlong generation, jbyteArray username, jbyteArray token) {
    uint8_t user_bytes[TMC_RA_ANDROID_MAX_USERNAME_BYTES] = {0};
    uint8_t token_bytes[TMC_RA_ANDROID_MAX_TOKEN_BYTES] = {0};
    size_t user_size = 0;
    size_t token_size = 0;

    (void)clazz;
    if (username == NULL || token == NULL) {
        WipeJavaByteArray(env, username);
        WipeJavaByteArray(env, token);
        return;
    }
    if (CopyJavaBytes(env, username, user_bytes, sizeof(user_bytes), &user_size)
        && CopyJavaBytes(env, token, token_bytes, sizeof(token_bytes), &token_size)) {
        pthread_mutex_lock(&sState.mutex);
        if (sState.queue_initialized)
            (void)TmcRaAndroidQueue_EnqueueToken(&sState.queue, (uint64_t)generation,
                                                 user_bytes, user_size, token_bytes, token_size);
        pthread_mutex_unlock(&sState.mutex);
    }
    WipeJavaByteArray(env, username);
    WipeJavaByteArray(env, token);
    Wipe(user_bytes, sizeof(user_bytes));
    Wipe(token_bytes, sizeof(token_bytes));
}

JNIEXPORT jboolean JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeBadgeImage(
    JNIEnv* env, jclass clazz, jlong generation, jstring url, jint width, jint height, jintArray pixels) {
    jsize pixel_count;
    uint32_t* argb;
    const char* url_chars;
    bool stored;
    TmcRaBadgeGate gate = {
        .lifecycle_mutex = &sState.lifecycle_mutex,
        .state_mutex = &sState.mutex,
        .state = &sState,
        .accept = BadgeIsCurrent,
    };

    (void)clazz;
    if (url == NULL || pixels == NULL || width < 1 || height < 1 ||
        width > TMC_RA_BADGE_MAX_DIMENSION || height > TMC_RA_BADGE_MAX_DIMENSION)
        return JNI_FALSE;
    pixel_count = (*env)->GetArrayLength(env, pixels);
    if (pixel_count != width * height)
        return JNI_FALSE;
    argb = malloc((size_t)pixel_count * sizeof(*argb));
    if (argb == NULL)
        return JNI_FALSE;
    (*env)->GetIntArrayRegion(env, pixels, 0, pixel_count, (jint*)argb);
    if ((*env)->ExceptionCheck(env)) {
        ClearException(env);
        memset(argb, 0, (size_t)pixel_count * sizeof(*argb));
        free(argb);
        return JNI_FALSE;
    }
    url_chars = (*env)->GetStringUTFChars(env, url, NULL);
    stored = url_chars != NULL &&
        TmcRaBadgeGate_PutArgb(&gate, (uint64_t)generation, url_chars, argb, width, height);
    if (url_chars != NULL)
        (*env)->ReleaseStringUTFChars(env, url, url_chars);
    if ((*env)->ExceptionCheck(env))
        ClearException(env);
    memset(argb, 0, (size_t)pixel_count * sizeof(*argb));
    free(argb);
    return stored ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jlong JNICALL Java_dev_picori_tmc_RAAndroidBridge_nativeGeneration(
    JNIEnv* env, jclass clazz) {
    (void)env;
    (void)clazz;
    return (jlong)TmcRaAndroidQueue_Generation(&sState.queue);
}
