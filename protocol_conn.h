#ifndef PROTOCOL_CONN_H
#define PROTOCOL_CONN_H

#include "core.h"
#include <cstdint>
#include <string>

// The resource managed by ISrsResourceManager.
class IResource
{
public:
    IResource();
    virtual ~IResource();
public:
    // Get the context id of connection.
    virtual const ContextId& GetId() = 0;
public:
    // The resource description, optional.
    virtual std::string Desc();
};

// The manager for resource.
class IResourceManager
{
public:
    IResourceManager();
    virtual ~IResourceManager();
public:
    // Remove then free the specified connection.
    virtual void Remove(IResource* c) = 0;
};

// The connection interface for all HTTP/RTMP/RTSP object.
class IConnection : public IResource
{
public:
    IConnection();
    virtual ~IConnection();
public:
    // Get remote ip address.
    virtual std::string RemoteIp() = 0;
};

// Lazy-sweep resource, never sweep util all wrappers are freed.
// See https://github.com/ossrs/srs/issues/3176#lazy-sweep
class LazyObject
{
private:
    // The reference count of resource, 0 is no wrapper and safe to sweep.
    int32_t m_gcRef;
    // The creator wrapper, which created this resource. Note that it might be disposed and the pointer is NULL, so be
    // careful and make sure to check it before use it.
    IResource* m_gcCreatorWrapper;
public:
    LazyObject();
    virtual ~LazyObject();
public:
    // For wrapper to use this resource.
    virtual LazyObject* GcUse();
    // For wrapper to dispose this resource.
    virtual LazyObject* GcDispose();
    // The current reference count of resource.
    virtual int32_t GcRef();
public:
    // Set the creator wrapper, from which resource clone wrapper.
    virtual void GcSetCreatorWrapper(IResource* wrapper);
    // Get the first available wrapper. NULL if the creator wrapper disposed.
    virtual IResource* GcCreatorWrapper();
};

// The lazy-sweep GC, wait for a long time to dispose resource even when resource is disposable.
// See https://github.com/ossrs/srs/issues/3176#lazy-sweep
class ILazyGc
{
public:
    ILazyGc();
    virtual ~ILazyGc();
public:
    // Remove then free the specified resource.
    virtual void Remove(LazyObject* c) = 0;
};


#endif // PROTOCOL_CONN_H
