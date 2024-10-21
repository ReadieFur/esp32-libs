#pragma once

#include "EServiceResult.h"
#include <mutex>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <algorithm>
#include <stack>
#include <map>
#include <unordered_set>

namespace ReadieFur::Services
{
    // template <typename TInstallParams>
    class AService
    {
    private:
        std::mutex _serviceMutex;
        std::unordered_set<std::type_index> _dependencies;
        std::map<std::type_index, AService*> _installedDependencies;
        bool _installed = false;
        bool _running = false;

        static int ImplWrapper(std::function<int()> func, std::mutex* mutex, bool& status, bool desiredStatus)
        {
            mutex->lock();
            if (status == desiredStatus)
            {
                mutex->unlock();
                return EServiceResult::Ok;
            }

            int retVal = func();
            if (retVal == EServiceResult::Ok)
                status = desiredStatus;

            mutex->unlock();
            return retVal;
        }

        bool ContainsCircularDependency(AService* service)
        {
            //This was originally a recursive method however I am using an iterative method to save space on the stack.
            std::unordered_set<AService*> visited;
            std::stack<AService*> toCheck;
            toCheck.push(service);

            while (!toCheck.empty())
            {
                AService* current = toCheck.top();
                toCheck.pop();

                //If we already visited this service then a circular dependency has been found.
                if (visited.count(current))
                    return true;

                visited.insert(current);

                //Check dependencies of the current service.
                for (auto&& dependency : current->_installedDependencies)
                {
                    if (dependency.second == this)
                        return true; //Circular dependency found.

                    toCheck.push(dependency.second);
                }
            }

            return false;
        }

    protected:
        std::unordered_set<AService*> _referencedBy;

        virtual int InstallServiceImpl() = 0;
        virtual int UninstallServiceImpl() = 0;
        virtual int StartServiceImpl() = 0;
        virtual int StopServiceImpl() = 0;

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, void>::type
        AddDependencyType()
        {
            _dependencies.insert(std::type_index(typeid(T)));
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, void>::type
        RemoveDependencyType()
        {
            _dependencies.erase(std::type_index(typeid(T)));
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, T*>::type
        GetDependency()
        {
            //Dependency must exist here.
            return reinterpret_cast<T*>(_installedDependencies[std::type_index(typeid(T))]);
        }

    public:
        bool IsInstalled()
        {
            _serviceMutex.lock();
            bool retVal = _installed;
            _serviceMutex.unlock();
            return retVal;
        }

        bool IsRunning()
        {
            _serviceMutex.lock();
            bool retVal = _running;
            _serviceMutex.unlock();
            return retVal;
        }

        int InstallService()
        {
            _serviceMutex.lock();

            //Check if all dependencies are satisfied.
            for (auto &&dependency : _dependencies)
            {
                if (_installedDependencies.find(dependency) == _installedDependencies.end())
                {
                    _serviceMutex.unlock();
                    return EServiceResult::MissingDependencies;
                }
            }
            //Not unlocking because we will lock again immediately within ImplWrapper (and unlocked in there too).

            //Make sure all dependencies are installed.
            for (auto &&dependency : _installedDependencies)
            {
                if (!dependency.second->IsInstalled())
                    return EServiceResult::DependencyNotReady;
            }
            
            return ImplWrapper([this](){ return InstallServiceImpl(); }, &_serviceMutex, _installed, true);
        }

        int UninstallService()
        {
            int stopServiceRes = StopService();
            if (stopServiceRes != EServiceResult::Ok)
                return stopServiceRes;

            return ImplWrapper([this](){ return UninstallServiceImpl(); }, &_serviceMutex, _installed, false);
        }

        int StartService()
        {
            if (!IsInstalled())
                return EServiceResult::NotInstalled;

            _serviceMutex.lock();
            for (auto &&dependency : _installedDependencies)
            {
                if (!dependency.second->IsRunning())
                    return EServiceResult::DependencyNotReady;
            }
            //Not unlocking because we will lock again immediately within ImplWrapper (and unlocked in there too).

            return ImplWrapper([this](){ return StartServiceImpl(); }, &_serviceMutex, _running, true);
        }

        int StopService(bool forceStop = false)
        {
            if (IsInstalled())
                return EServiceResult::NotInstalled;

            _serviceMutex.lock();
            for (auto &&reference : _referencedBy)
            {
                if (reference->IsRunning())
                {
                    if (!forceStop)
                    {
                        _serviceMutex.unlock();
                        return EServiceResult::InUse;
                    }

                    reference->StopService(forceStop);
                }
            }
            

            return ImplWrapper([this](){ return StopServiceImpl(); }, &_serviceMutex, _running, false);
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, bool>::type
        AddDependency(T* service)
        {
            if (service == nullptr
                || _dependencies.find(std::type_index(typeid(T))) == _dependencies.end() /*Provided dependency is not needed.*/
                || _installedDependencies.find(std::type_index(typeid(T))) != _installedDependencies.end() /*Service for the specified dependency has already been installed.*/
                || ContainsCircularDependency(service) /*Check if a circular dependency exists.*/)
                return false;

            _installedDependencies[std::type_index(typeid(T))] = service;
            service->_referencedBy.insert(this);
            return true;
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, void>::type
        RemoveDependency()
        {
            auto kvp = _installedDependencies.find(std::type_index(typeid(T)));
            if (kvp == _installedDependencies.end())
                return;

            _installedDependencies.erase(kvp->first);
            kvp->second->_referencedBy.erase(this);
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, void>::type
        RemoveDependency(T* service)
        {
            for (auto &&kvp : _installedDependencies)
            {
                if (kvp.second == service)
                {
                    _installedDependencies.erase(kvp.first);
                    service->_referencedBy.erase(this);
                    return;
                }
            }
        }
    };
};
