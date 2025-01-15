#pragma once

#include "AService.hpp"
#include "EServiceResult.h"
#include <mutex>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <algorithm>
#include <stack>
#include <map>
#include <vector>

namespace ReadieFur::Service
{
    class ServiceManager
    {
    // friend class ReadieFur::Diagnostic::DiagnosticsService;
    private:
        static std::mutex _mutex;
        static std::map<std::type_index, AService*> _services;
        static std::map<std::type_index, std::vector<AService*>> _references;

        static AService* GetServiceInternal(std::type_index type)
        {
            // _mutex.lock();

            auto service = _services.find(type);
            if (service == _services.end())
            {
                _mutex.unlock();
                return nullptr;
            }

            // _mutex.unlock();
            return service->second;
        }

    public:
        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static InstallService()
        {
            _mutex.lock();

            if (_services.find(std::type_index(typeid(T))) != _services.end())
            {
                _mutex.unlock();
                return EServiceResult::AlreadyInstalled;
            }

            //It isn't possible for a circular dependency to exist here because this service doesn't exist in the list yet.
            AService* service = reinterpret_cast<AService*>(new T());
            service->_getServiceCallback = GetServiceInternal;
            
            //Check if all dependencies are satisfied.
            std::vector<std::vector<AService*>*> dependenciesToAddTo;
            for (auto &&dependency : service->_dependencies)
            {
                if (_services.find(dependency) == _services.end())
                {
                    _mutex.unlock();
                    delete service;
                    return EServiceResult::MissingDependencies;
                }

                //Store deps here temporaily so that if the check above fails, we dont need to filter through the list and remove the new service.
                dependenciesToAddTo.push_back(&_references[dependency]);
            }
            for (auto &&dependency : dependenciesToAddTo)
                dependency->push_back(service);

            _services[std::type_index(typeid(T))] = service;

            _mutex.unlock();
            return EServiceResult::Ok;
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static UninstallService()
        {
            _mutex.lock();

            auto service = _services.find(std::type_index(typeid(T)));
            if (service == _services.end())
            {
                _mutex.unlock();
                return EServiceResult::NotInstalled;
            }

            if (service->second->IsRunning())
            {
                _mutex.unlock();
                return EServiceResult::InUse;
            }

            //Make sure no other services depend on this service.
            auto serviceReferences = _references.find(std::type_index(typeid(T)));
            if (serviceReferences != _references.end() && serviceReferences->second.size() != 0)
            {
                _mutex.unlock();
                return EServiceResult::InUse;
            }
            _references.erase(std::type_index(typeid(T)));

            delete service->second;
            _services.erase(std::type_index(typeid(T)));

            _mutex.unlock();
            return EServiceResult::Ok;
        }

        static EServiceResult StartService(std::type_index type)
        {
            _mutex.lock();

            auto service = _services.find(type);
            if (service == _services.end())
            {
                _mutex.unlock();
                return EServiceResult::NotInstalled;
            }

            if (service->second->IsRunning())
            {
                _mutex.unlock();
                return EServiceResult::Ok;
            }

            //Make sure all required dependencies are running.
            for (auto &&dependency : service->second->_dependencies)
            {
                //The dependency SHOULD exist here so no need to check if it doesn't.
                if (!_services[dependency]->IsRunning())
                {
                    _mutex.unlock();
                    return EServiceResult::DependencyNotReady;
                }
            }

            EServiceResult retVal = service->second->StartService();

            _mutex.unlock();
            return retVal;
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static StartService()
        {
            return StartService(std::type_index(typeid(T)));
        }

        static EServiceResult StopService(std::type_index type)
        {
            _mutex.lock();

            auto service = _services.find(type);
            if (service == _services.end())
            {
                _mutex.unlock();
                return EServiceResult::NotInstalled;
            }

            if (!service->second->IsRunning())
            {
                _mutex.unlock();
                return EServiceResult::Ok;
            }

            auto refService = _references.find(type);
            //Make sure none of the services that depend on this are running.
            for (auto &&reference : refService->second)
            {
                if (reference->IsRunning())
                {
                    _mutex.unlock();
                    return EServiceResult::InUse;
                }
            }

            EServiceResult retVal = service->second->StopService();

            _mutex.unlock();
            return retVal;
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static StopService()
        {
            return StopService(std::type_index(typeid(T)));
        }

        static EServiceResult SuspendService(std::type_index type)
        {
            _mutex.lock();

            auto service = _services.find(type);
            if (service == _services.end())
            {
                _mutex.unlock();
                return EServiceResult::NotInstalled;
            }

            if (!service->second->IsRunning())
            {
                _mutex.unlock();
                return EServiceResult::NotReady;
            }

            auto refService = _references.find(type);
            //Make sure none of the services that depend on this are running.
            for (auto &&reference : refService->second)
            {
                if (reference->IsRunning())
                {
                    _mutex.unlock();
                    return EServiceResult::InUse;
                }
            }

            //TODO: Currently this will only suspend the main task, if there are other tasks that are created by the service then they will not be suspended.
            vTaskSuspend(service->second->_taskHandle);

            _mutex.unlock();
            return EServiceResult::Ok;
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static SuspendService()
        {
            return SuspendService(std::type_index(typeid(T)));
        }

        static EServiceResult ResumeService(std::type_index type)
        {
            _mutex.lock();

            auto service = _services.find(type);
            if (service == _services.end())
            {
                _mutex.unlock();
                return EServiceResult::NotInstalled;
            }

            if (service->second->IsRunning())
            {
                _mutex.unlock();
                return EServiceResult::Ok;
            }

            //No need to check on services that depend on this as they should already be stopped in this state.

            vTaskResume(service->second->_taskHandle);

            _mutex.unlock();
            return EServiceResult::Ok;
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static ResumeService()
        {
            return ResumeService(std::type_index(typeid(T)));
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, EServiceResult>::type
        static InstallAndStartService()
        {
            EServiceResult result = InstallService<T>();
            if (result != EServiceResult::Ok)
                return result;
            return StartService<T>();
        }

        template <typename T>
        typename std::enable_if<std::is_base_of<AService, T>::value, T*>::type
        static GetService()
        {
            _mutex.lock();
            T* retVal = reinterpret_cast<T*>(GetServiceInternal(std::type_index(typeid(T))));
            _mutex.unlock();
            return retVal;
        }

        static AService* GetService(std::type_index type)
        {
            _mutex.lock();
            AService* retVal = GetServiceInternal(type);
            _mutex.unlock();
            return retVal;
        }

        /// @brief Get a service list by dependencies, where the first service in the list is the one that must be started first and the last is the one with no dependencies.
        static std::vector<std::type_index> GetServices()
        {
            _mutex.lock();

            std::vector<std::type_index> services;
            for (auto &&service : _services)
                services.push_back(service.first);

            //This was originally a recursive method however I am using an iterative method to save space on the stack.
            std::unordered_set<std::type_index> visited;
            std::stack<std::type_index> toCheck;
            for (auto &&service : services)
                toCheck.push(service);

            std::vector<std::type_index> sortedServices;
            while (!toCheck.empty())
            {
                std::type_index current = toCheck.top();
                toCheck.pop();

                //If we already visited this service then a circular dependency has been found.
                if (visited.count(current))
                {
                    _mutex.unlock();
                    return {};
                }

                visited.insert(current);

                //Check dependencies of the current service.
                auto service = _services.find(current);
                for (auto &&dependency : service->second->_dependencies)
                {
                    if (std::find(sortedServices.begin(), sortedServices.end(), dependency) == sortedServices.end())
                        toCheck.push(dependency);
                }

                sortedServices.push_back(current);
            }

            _mutex.unlock();

            return sortedServices;
        }
    };
};

std::mutex ReadieFur::Service::ServiceManager::_mutex;
std::map<std::type_index, ReadieFur::Service::AService*> ReadieFur::Service::ServiceManager::_services;
std::map<std::type_index, std::vector<ReadieFur::Service::AService*>> ReadieFur::Service::ServiceManager::_references;
