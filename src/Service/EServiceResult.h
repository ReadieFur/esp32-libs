#pragma once

namespace ReadieFur::Service
{
    enum EServiceResult
    {
        Ok,
        Failed,
        NotInstalled,
        InUse,
        MissingDependencies,
        DependencyNotReady,
        AlreadyInstalled,
        Timeout
    };
};
