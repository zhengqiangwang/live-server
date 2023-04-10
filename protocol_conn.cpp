#include "protocol_conn.h"

IResource::IResource()
{

}

IResource::~IResource()
{

}

std::string IResource::Desc()
{
    return "Resource";
}

IResourceManager::IResourceManager()
{

}

IResourceManager::~IResourceManager()
{

}

IConnection::IConnection()
{

}

IConnection::~IConnection()
{

}

LazyObject::LazyObject()
{
    m_gcRef = 0;
    m_gcCreatorWrapper = NULL;
}

LazyObject::~LazyObject()
{

}

LazyObject *LazyObject::GcUse()
{
    m_gcRef++;
    return this;
}

LazyObject *LazyObject::GcDispose()
{
    m_gcRef--;
    return this;
}

int32_t LazyObject::GcRef()
{
    return m_gcRef;
}

void LazyObject::GcSetCreatorWrapper(IResource *wrapper)
{
    m_gcCreatorWrapper = wrapper;
}

IResource *LazyObject::GcCreatorWrapper()
{
    return m_gcCreatorWrapper;
}

ILazyGc::ILazyGc()
{

}

ILazyGc::~ILazyGc()
{

}
