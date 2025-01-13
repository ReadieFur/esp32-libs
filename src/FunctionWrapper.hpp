#pragma once

#include <functional>

namespace ReadieFur
{
    template<typename RetType, typename... ArgTypes>
    class FunctionWrapper
    {
    private:
        std::function<RetType(ArgTypes...)> _func;

    public:
        FunctionWrapper(std::function<RetType(ArgTypes...)> func) : _func(func) {}
        
        FunctionWrapper(RetType(*func)(ArgTypes...)) : _func(func) {}

        //Call operator.
        RetType operator()(ArgTypes... args)
        {
            return _func(args...);
        }

        //std::function conversion operator.
        operator std::function<RetType(ArgTypes...)>() const
        {
            return _func;
        }

        //Function pointer conversion operator.
        operator RetType(*)(ArgTypes...)() const
        {
            return _func.target<RetType(*)(ArgTypes...)>();
        }
    };
};
