#include "../BlackGlass.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    std::string cachePath()
    {
        return std::string(BLACK_GLASS_LOG_DIR) + "/pipeline_cache.bin";
    }
}

void BlackGlass::createPipelineCache()
{
    std::vector<char> data;
    {
        std::ifstream file(cachePath(), std::ios::binary);
        if (file.is_open())
        {
            data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
    }

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = data.size();
    cacheInfo.pInitialData = data.empty() ? nullptr : data.data();

    if (vkCreatePipelineCache(M_DEVICE, &cacheInfo, nullptr, &M_PIPELINECACHE) != VK_SUCCESS)
    {
        M_PIPELINECACHE = VK_NULL_HANDLE;
        if (!data.empty())
        {
            cacheInfo.initialDataSize = 0;
            cacheInfo.pInitialData = nullptr;
            if (vkCreatePipelineCache(M_DEVICE, &cacheInfo, nullptr, &M_PIPELINECACHE) != VK_SUCCESS)
            {
                M_PIPELINECACHE = VK_NULL_HANDLE;
            }
        }
    }

    std::cout << "  [createPipelineCache] " << data.size() << " bytes loaded.\n";
}

void BlackGlass::savePipelineCache()
{
    if (M_PIPELINECACHE == VK_NULL_HANDLE)
    {
        return;
    }

    size_t size = 0;
    if (vkGetPipelineCacheData(M_DEVICE, M_PIPELINECACHE, &size, nullptr) != VK_SUCCESS || size == 0)
    {
        return;
    }

    std::vector<char> data(size);
    if (vkGetPipelineCacheData(M_DEVICE, M_PIPELINECACHE, &size, data.data()) != VK_SUCCESS)
    {
        return;
    }
    data.resize(size);

    std::error_code error;
    std::filesystem::create_directories(BLACK_GLASS_LOG_DIR, error);

    const std::string path = cachePath();
    const std::string temp = path + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            return;
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file.good())
        {
            return;
        }
    }

    std::filesystem::rename(temp, path, error);
    if (error)
    {
        std::filesystem::remove(temp, error);
        return;
    }

    std::cout << "  [savePipelineCache] " << data.size() << " bytes written.\n";
}

void BlackGlass::destroyPipelineCache()
{
    if (M_PIPELINECACHE != VK_NULL_HANDLE)
    {
        vkDestroyPipelineCache(M_DEVICE, M_PIPELINECACHE, nullptr);
        M_PIPELINECACHE = VK_NULL_HANDLE;
    }
}
