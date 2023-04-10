#ifndef CORE_AUTOFREE_H
#define CORE_AUTOFREE_H

#include <cstdlib>

// To free the instance in the current scope, for instance, MyClass* ptr,
// which is a ptr and this class will:
//       1. free the ptr.
//       2. set ptr to NULL.
//
// Usage:
//       MyClass* po = new MyClass();
//       // ...... use po
//       AutoFree(MyClass, po);
//
// Usage for array:
//      MyClass** pa = new MyClass*[size];
//      // ....... use pa
//      AutoFreeA(MyClass*, pa);
//
// @remark the MyClass can be basic type, for instance, AutoFreeA(char, pstr),
//      where the char* pstr = new char[size].
// To delete object.
#define AutoFree(className, instance) \
    impl_AutoFree<className> _auto_free_##instance(&instance, false, false, NULL)
// To delete array.
#define AutoFreeA(className, instance) \
    impl_AutoFree<className> _auto_free_array_##instance(&instance, true, false, NULL)
// Use free instead of delete.
#define AutoFreeF(className, instance) \
    impl_AutoFree<className> _auto_free_##instance(&instance, false, true, NULL)
// Use hook instead of delete.
#define AutoFreeH(className, instance, hook) \
    impl_AutoFree<className> _auto_free_##instance(&instance, false, false, hook)
// The template implementation.
template<class T>
class impl_AutoFree
{
private:
    T** m_ptr;
    bool m_isArray;
    bool m_useFree;
    void (*m_hook)(T*);
public:
    // If use_free, use free(void*) to release the p.
    // If specified hook, use hook(p) to release it.
    // Use delete to release p, or delete[] if p is an array.
    impl_AutoFree(T** p, bool array, bool use_free, void (*hook)(T*)) {
        m_ptr = p;
        m_isArray = array;
        m_useFree = use_free;
        m_hook = hook;
    }

    virtual ~impl_AutoFree() {
        if (m_ptr == nullptr || *m_ptr == nullptr) {
            return;
        }

        if (m_useFree) {
            free(*m_ptr);
        } else if (m_hook) {
            m_hook(*m_ptr);
        } else {
            if (m_isArray) {
                delete[] *m_ptr;
            } else {
                delete *m_ptr;
            }
        }

        *m_ptr = nullptr;
    }
};

#endif // CORE_AUTOFREE_H
