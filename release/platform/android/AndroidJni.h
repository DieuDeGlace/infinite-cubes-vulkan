#pragma once
#include <jni.h>

namespace android {

// Each JNI call releases its local references. Only detach threads that this scope attached.
class JniFrame {
public:
    explicit JniFrame(JavaVM *vm)
        : m_vm(vm)
    {
        const jint status = vm->GetEnv(reinterpret_cast<void **>(&m_env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            m_attached = vm->AttachCurrentThread(&m_env, nullptr) == JNI_OK;
            if (!m_attached)
                m_env = nullptr;
        }
        if (m_env && m_env->PushLocalFrame(16) != JNI_OK) {
            m_env->ExceptionClear();
            m_env = nullptr;
        }
    }

    ~JniFrame()
    {
        if (m_env)
            m_env->PopLocalFrame(nullptr);
        if (m_attached)
            m_vm->DetachCurrentThread();
    }

    JniFrame(const JniFrame &) = delete;
    JniFrame &operator=(const JniFrame &) = delete;
    JNIEnv *get() const { return m_env; }

private:
    JavaVM *m_vm;
    JNIEnv *m_env = nullptr;
    bool m_attached = false;
};

}
