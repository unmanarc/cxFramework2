#ifndef DATETIME_H
#define DATETIME_H

#include "a_var.h"
#include <time.h>
#include <cx2_thr_mutex/mutex_shared.h>

namespace CX2 { namespace Memory { namespace Abstract {

class DATETIME: public Var
{
public:
    DATETIME();
    DATETIME(const time_t & value);
    DATETIME& operator=(const time_t & value)
    {
        setValue(value);
        return *this;
    }

    time_t getValue();
    bool setValue(const time_t & value);

    void * getDirectMemory() override { return &value; }
    std::string toString() override;
    bool fromString(const std::string & value) override;
protected:
    Var * protectedCopy() override;

private:
    std::string getISOTimeStr( const time_t & v );
    time_t fromISOTimeStr( const std::string & v );

    time_t value;
    Threads::Sync::Mutex_Shared mutex;

};
}}}

#endif // DATETIME_H
