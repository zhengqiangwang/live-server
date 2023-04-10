#include "core.h"

ContextId::ContextId()
{

}

ContextId::ContextId(const ContextId &cp)
{
    m_id = cp.m_id;
}

ContextId &ContextId::operator=(const ContextId &cp)
{
    m_id = cp.m_id;
    return *this;
}

ContextId::~ContextId()
{

}

const char *ContextId::Cstr() const
{
    return m_id.c_str();
}

bool ContextId::Empty() const
{
    return m_id.empty();
}

int ContextId::Compare(const ContextId &to) const
{
    return m_id.compare(to.m_id);
}

ContextId &ContextId::SetValue(const std::string &id)
{
    m_id = id;
    return *this;
}
