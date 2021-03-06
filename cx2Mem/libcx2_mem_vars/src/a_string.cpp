#include "a_string.h"
#include <cx2_thr_mutex/lock_shared.h>

using namespace CX2::Memory::Abstract;

STRING::STRING()
{
    setVarType(TYPE_STRING);
}

STRING::STRING(const std::string &value)
{
    setVarType(TYPE_STRING);
    setValue(value);
}



std::string STRING::getValue()
{
    Threads::Sync::Lock_RD lock(mutex);
    return value;
}

bool STRING::setValue(const std::string &value)
{
    return fromString(value);
}

std::string STRING::toString()
{
    return getValue();
}

bool STRING::fromString(const std::string &value)
{
    Threads::Sync::Lock_RW lock(mutex);
    this->value = value;
    return true;
}

Var *STRING::protectedCopy()
{
    Threads::Sync::Lock_RD lock(mutex);
    STRING * var = new STRING;
    if (var) *var = this->value;
    return var;
}
