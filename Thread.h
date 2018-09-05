//
// This is a class that represents a thread.
//

#ifndef OS_EX2_THREAD_H
#define OS_EX2_THREAD_H

#include <iostream>
#include "uthreads.h"

class Thread {
public:
    Thread(std::string _state, unsigned int _tid, void (*f)(void)) : _state(std::move(_state)), _tid(_tid),
                                                                     _function(f) {
        _quantums_num = 0;
        _is_synced = false;
    }

    virtual ~Thread() = default;

    std::string getState() const {
        return _state;
    }

    void setState(std::string state) {
        Thread::_state = std::move(state);
    }

    unsigned int getTid() const {
        return _tid;
    }

    void setTid(unsigned int tid) {
        Thread::_tid = tid;
    }

    const char *getAllocated_mem() const {
        return _allocated_mem;
    }

    unsigned int get_quantums_num() const
    {
        return _quantums_num;
    }

    void raise_quantums()
    {
        _quantums_num++;
    }

    void switch_sync_state() {
        _is_synced = !_is_synced;
    }

    bool get_is_synced() const {
        return _is_synced;
    }

private:
    unsigned int _tid;
    std::string _state;
    void (*_function)(void);
    unsigned int _quantums_num;
    bool _is_synced;
    char _allocated_mem[STACK_SIZE];
};


#endif //OS_EX2_THREAD_H
