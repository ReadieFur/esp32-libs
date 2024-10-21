#pragma once

namespace ReadieFur::Services
{
    enum EServiceResult
    {
        Ok = 0,
        NotInstalled = -1,
        InUse = -2,
        MissingDependencies = -3,
        DependencyNotReady = -4
    };
};
