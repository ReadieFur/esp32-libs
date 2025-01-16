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
#include <unordered_set>
#include <queue>
#include "Logging.hpp"

namespace ReadieFur::Service
{
    class ServiceManager
    {
    // friend class ReadieFur::Diagnostic::DiagnosticsService;
    private:
        static std::mutex _mutex;
        // static std::vector<std::type_index> _orderedServices; //Increases memory usage slightly but means I don't need to figure out an algorithm for sorting the services by dependencies as this is restricted by the service installation.
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
            // _orderedServices.push_back(std::type_index(typeid(T)));

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
            // _orderedServices.erase(std::remove(_orderedServices.begin(), _orderedServices.end(), std::type_index(typeid(T))), _orderedServices.end());

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

        /// @brief Get a service list by dependencies, where the first service in the list is the one that must be started first/ended last and the last is the one that must be started last/ended first.
        //https://www.geeksforgeeks.org/topological-sorting-indegree-based-solution/
        //This algorithm wouldn't strictly be needed if I stored the services in a sorted order due to services having to be installed in the order of their dependencies, but it is good to have this anyway.
        static std::vector<std::type_index> GetServices()
        {
            _mutex.lock();

            std::map<std::type_index, std::vector<std::type_index>> dependencyGraph;
            std::unordered_map<std::type_index, int> inDegree;
            std::vector<std::type_index> sortedOrder;

            //Build the dependency graph and initialize in-degrees.
            for (const auto& [serviceType, service] : _services)
            {
                if (!inDegree.count(serviceType))
                    inDegree[serviceType] = 0;

                for (const auto& dependency : service->_dependencies)
                {
                    dependencyGraph[dependency].push_back(serviceType);
                    inDegree[serviceType]++;
                    if (!inDegree.count(dependency))
                        inDegree[dependency] = 0;
                }
            }

            //Use a priority queue for zero-in-degree services (i.e. services that have no dependencies go first).
            std::priority_queue<std::type_index, std::vector<std::type_index>, std::greater<>> zeroInDegreeQueue;
            for (const auto& [type, degree] : inDegree)
                if (degree == 0)
                    zeroInDegreeQueue.push(type);

            //Perform topological sort.
            while (!zeroInDegreeQueue.empty())
            {
                std::type_index current = zeroInDegreeQueue.top();
                zeroInDegreeQueue.pop();
                sortedOrder.push_back(current);

                for (const auto& dependent : dependencyGraph[current])
                {
                    inDegree[dependent]--;
                    if (inDegree[dependent] == 0)
                        zeroInDegreeQueue.push(dependent);
                }
            }

            //Check for circular dependencies.
            if (sortedOrder.size() != inDegree.size())
            {
                LOGE(nameof(ServiceManager), "Circular dependency detected.");
                abort();
            }

            _mutex.unlock();
            return sortedOrder;
        }
    };
};

std::mutex ReadieFur::Service::ServiceManager::_mutex;
std::map<std::type_index, ReadieFur::Service::AService*> ReadieFur::Service::ServiceManager::_services;
std::map<std::type_index, std::vector<ReadieFur::Service::AService*>> ReadieFur::Service::ServiceManager::_references;
