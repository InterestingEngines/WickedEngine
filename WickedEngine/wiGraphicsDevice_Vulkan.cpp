#define _CRT_SECURE_NO_WARNINGS
#include "wiGraphicsDevice_Vulkan.h"
#include "wiGraphicsDevice_SharedInternals.h"
#include "wiHelper.h"
#include "ShaderInterop_Vulkan.h"

#include <sstream>
#include <vector>
#include <cstring>
#include <iostream>
#include <set>



#ifdef WICKEDENGINE_BUILD_VULKAN
#pragma comment(lib,"vulkan-1.lib")

namespace wiGraphicsTypes
{

	// Validation layer helpers:
	const std::vector<const char*> validationLayers = {
		"VK_LAYER_LUNARG_standard_validation"
	};
#ifdef NDEBUG
	bool enableValidationLayers = false;
#else
	bool enableValidationLayers = true;
#endif
	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

		std::vector<VkLayerProperties> availableLayers(layerCount);
		vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

		for (const char* layerName : validationLayers) {
			bool layerFound = false;

			for (const auto& layerProperties : availableLayers) {
				if (strcmp(layerName, layerProperties.layerName) == 0) {
					layerFound = true;
					break;
				}
			}

			if (!layerFound) {
				return false;
			}
		}

		return true;
	}
	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugReportFlagsEXT flags,
		VkDebugReportObjectTypeEXT objType,
		uint64_t obj,
		size_t location,
		int32_t code,
		const char* layerPrefix,
		const char* msg,
		void* userData) {

		std::stringstream ss("");
		ss << "[VULKAN validation layer]: " << msg << std::endl;

		std::cerr << ss.str();
		OutputDebugStringA(ss.str().c_str());

		return VK_FALSE;
	}
	VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback) {
		auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
		if (func != nullptr) {
			return func(instance, pCreateInfo, pAllocator, pCallback);
		}
		else {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}
	}
	void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator) {
		auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
		if (func != nullptr) {
			func(instance, callback, pAllocator);
		}
	}

	// Queue families:
	struct QueueFamilyIndices {
		int graphicsFamily = -1;
		int presentFamily = -1;
		int copyFamily = -1;

		bool isComplete() {
			return graphicsFamily >= 0 && presentFamily >= 0 && copyFamily >= 0;
		}
	};
	QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
		QueueFamilyIndices indices;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

		std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

		int i = 0;
		for (const auto& queueFamily : queueFamilies) {
			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
			if (queueFamily.queueCount > 0 && presentSupport) {
				indices.presentFamily = i;
			}

			if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				indices.graphicsFamily = i;
			}

			if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) {
				indices.copyFamily = i;
			}

			if (indices.isComplete()) {
				break;
			}

			i++;
		}

		return indices;
	}

	// Swapchain helpers:
	const std::vector<const char*> deviceExtensions = {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};
	bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

		std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

		for (const auto& extension : availableExtensions) {
			requiredExtensions.erase(extension.extensionName);
		}

		return requiredExtensions.empty();
	}

	struct SwapChainSupportDetails {
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
		SwapChainSupportDetails details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

		if (formatCount != 0) {
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
		}

		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

		if (presentModeCount != 0) {
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
		if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		for (const auto& availableFormat : availableFormats) {
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return availableFormat;
			}
		}

		return availableFormats[0];
	}
	VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR> availablePresentModes) {
		VkPresentModeKHR bestMode = VK_PRESENT_MODE_FIFO_KHR;

		for (const auto& availablePresentMode : availablePresentModes) {
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return availablePresentMode;
			}
			else if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
				bestMode = availablePresentMode;
			}
		}

		return bestMode;
	}

	uint32_t findMemoryType(VkPhysicalDevice device, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(device, &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}

		throw std::runtime_error("failed to find suitable memory type!");
	}

	// Device selection helpers:
	bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
		QueueFamilyIndices indices = findQueueFamilies(device, surface);

		bool extensionsSupported = checkDeviceExtensionSupport(device);

		bool swapChainAdequate = false;
		if (extensionsSupported) {
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
			swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		return indices.isComplete() && extensionsSupported && swapChainAdequate;
	}



	// Memory tools:

	inline size_t Align(size_t uLocation, size_t uAlign)
	{
		if ((0 == uAlign) || (uAlign & (uAlign - 1)))
		{
			assert(0);
		}

		return ((uLocation + (uAlign - 1)) & ~(uAlign - 1));
	}




	GraphicsDevice_Vulkan::FrameResources::ResourceFrameAllocator::ResourceFrameAllocator(VkPhysicalDevice physicalDevice, VkDevice device, size_t size) : device(device)
	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.flags = 0;

		VkResult res = vkCreateBuffer(device, &bufferInfo, nullptr, &resource);
		assert(res == VK_SUCCESS);


		// Allocate resource backing memory:
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, resource, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &resourceMemory) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate staging memory!");
		}

		res = vkBindBufferMemory(device, resource, resourceMemory, 0);
		assert(res == VK_SUCCESS);




		void* pData;
		//
		// No CPU reads will be done from the resource.
		//
		vkMapMemory(device, resourceMemory, 0, bufferInfo.size, 0, &pData);
		dataCur = dataBegin = reinterpret_cast< UINT8* >(pData);
		dataEnd = dataBegin + size;
	}
	GraphicsDevice_Vulkan::FrameResources::ResourceFrameAllocator::~ResourceFrameAllocator()
	{
		vkDestroyBuffer(device, resource, nullptr);
	}
	uint8_t* GraphicsDevice_Vulkan::FrameResources::ResourceFrameAllocator::allocate(size_t dataSize, size_t alignment)
	{
		dataCur = reinterpret_cast<uint8_t*>(Align(reinterpret_cast<size_t>(dataCur), alignment));
		assert(dataCur + dataSize <= dataEnd);

		uint8_t* retVal = dataCur;

		dataCur += dataSize;

		return retVal;
	}
	void GraphicsDevice_Vulkan::FrameResources::ResourceFrameAllocator::clear()
	{
		dataCur = dataBegin;
	}
	uint64_t GraphicsDevice_Vulkan::FrameResources::ResourceFrameAllocator::calculateOffset(uint8_t* address)
	{
		assert(address >= dataBegin && address < dataEnd);
		return static_cast<uint64_t>(address - dataBegin);
	}



	GraphicsDevice_Vulkan::UploadBuffer::UploadBuffer(VkPhysicalDevice physicalDevice, VkDevice device, size_t size) : device(device)
	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = size;
		bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferInfo.flags = 0;

		VkResult res = vkCreateBuffer(device, &bufferInfo, nullptr, &resource);
		assert(res == VK_SUCCESS);


		// Allocate resource backing memory:
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, resource, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, 
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		if (vkAllocateMemory(device, &allocInfo, nullptr, &resourceMemory) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate staging memory!");
		}

		res = vkBindBufferMemory(device, resource, resourceMemory, 0);
		assert(res == VK_SUCCESS);




		void* pData;
		//
		// No CPU reads will be done from the resource.
		//
		vkMapMemory(device, resourceMemory, 0, bufferInfo.size, 0, &pData);
		dataCur = dataBegin = reinterpret_cast< UINT8* >(pData);
		dataEnd = dataBegin + size;
	}
	GraphicsDevice_Vulkan::UploadBuffer::~UploadBuffer()
	{
		vkDestroyBuffer(device, resource, nullptr);
	}
	uint8_t* GraphicsDevice_Vulkan::UploadBuffer::allocate(size_t dataSize, size_t alignment)
	{
		LOCK();

		//dataCur = reinterpret_cast<uint8_t*>(Align(reinterpret_cast<size_t>(dataCur), alignment));

		dataSize = Align(dataSize, alignment);
		assert(dataCur + dataSize <= dataEnd);

		uint8_t* retVal = dataCur;

		dataCur += dataSize;

		UNLOCK();

		return retVal;
	}
	void GraphicsDevice_Vulkan::UploadBuffer::clear()
	{
		dataCur = dataBegin;
	}
	uint64_t GraphicsDevice_Vulkan::UploadBuffer::calculateOffset(uint8_t* address)
	{
		assert(address >= dataBegin && address < dataEnd);
		return static_cast<uint64_t>(address - dataBegin);
	}




	GraphicsDevice_Vulkan::FrameResources::DescriptorTableFrameAllocator::DescriptorTableFrameAllocator(GraphicsDevice_Vulkan* device, UINT maxRenameCount) : device(device)
	{
		// Create descriptor pool:
		{
			uint32_t numTables = SHADERSTAGE_COUNT * (maxRenameCount + 1); // (gpu * maxRenameCount) + (1 * cpu staging table)

			VkDescriptorPoolSize tableLayout[8] = {};

			tableLayout[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			tableLayout[0].descriptorCount = GPU_RESOURCE_HEAP_CBV_COUNT;

			tableLayout[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			tableLayout[1].descriptorCount = GPU_RESOURCE_HEAP_SRV_COUNT;

			tableLayout[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
			tableLayout[2].descriptorCount = GPU_RESOURCE_HEAP_SRV_COUNT;

			tableLayout[3].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			tableLayout[3].descriptorCount = GPU_RESOURCE_HEAP_SRV_COUNT;

			tableLayout[4].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			tableLayout[4].descriptorCount = GPU_RESOURCE_HEAP_UAV_COUNT;

			tableLayout[5].type = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
			tableLayout[5].descriptorCount = GPU_RESOURCE_HEAP_UAV_COUNT;

			tableLayout[6].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			tableLayout[6].descriptorCount = GPU_RESOURCE_HEAP_UAV_COUNT;

			tableLayout[7].type = VK_DESCRIPTOR_TYPE_SAMPLER;
			tableLayout[7].descriptorCount = GPU_SAMPLER_HEAP_COUNT;


			std::vector<VkDescriptorPoolSize> poolSizes;
			poolSizes.reserve(ARRAYSIZE(tableLayout) * numTables);
			for (uint32_t i = 0; i < numTables; ++i)
			{
				for (int j = 0; j < ARRAYSIZE(tableLayout); ++j)
				{
					poolSizes.push_back(tableLayout[j]);
				}
			}


			VkDescriptorPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
			poolInfo.pPoolSizes = poolSizes.data();
			poolInfo.maxSets = numTables;
			//poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

			if (vkCreateDescriptorPool(device->device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
				throw std::runtime_error("failed to create descriptor pool!");
			}

		}


		// Create staging descriptor table:
		{
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = SHADERSTAGE_COUNT;
			allocInfo.pSetLayouts = device->defaultDescriptorSetlayouts;

			if (vkAllocateDescriptorSets(device->device, &allocInfo, descriptorSet_CPU) != VK_SUCCESS) {
				throw std::runtime_error("failed to allocate descriptor set!");
			}
		}

		// Create GPU-visible descriptor tables:
		{
			VkDescriptorSetAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocInfo.descriptorPool = descriptorPool;
			allocInfo.descriptorSetCount = 1;


			for (int stage = 0; stage < SHADERSTAGE_COUNT; ++stage)
			{
				allocInfo.pSetLayouts = &device->defaultDescriptorSetlayouts[stage];
				descriptorSet_GPU[stage].resize(SHADERSTAGE_COUNT * maxRenameCount);

				for (uint32_t i = 0; i < maxRenameCount; ++i)
				{
					if (vkAllocateDescriptorSets(device->device, &allocInfo, &descriptorSet_GPU[stage][i]) != VK_SUCCESS) {
						throw std::runtime_error("failed to allocate descriptor set!");
					}
				}
			}

		}




		// invalidate descriptors in flight:
		for (int stage = 0; stage < SHADERSTAGE_COUNT; ++stage)
		{
			ringOffset[stage] = 0;
			dirty[stage] = false;
		}

	}
	GraphicsDevice_Vulkan::FrameResources::DescriptorTableFrameAllocator::~DescriptorTableFrameAllocator()
	{
		vkDestroyDescriptorPool(device->device, descriptorPool, nullptr);
	}
	void GraphicsDevice_Vulkan::FrameResources::DescriptorTableFrameAllocator::reset()
	{
		for (int stage = 0; stage < SHADERSTAGE_COUNT; ++stage)
		{
			ringOffset[stage] = 0;
			dirty[stage] = true; 
			

			// STAGING CPU descriptor table needs to be initialized:

			VkDescriptorBufferInfo bufferInfo[GPU_RESOURCE_HEAP_CBV_COUNT] = {};
			for (int i = 0; i < ARRAYSIZE(bufferInfo); ++i)
			{
				bufferInfo[i].buffer = device->nullBuffer;
				bufferInfo[i].offset = 0;
				bufferInfo[i].range = VK_WHOLE_SIZE;
			}

			VkWriteDescriptorSet writeDescriptors = {};

			writeDescriptors.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptors.dstSet = descriptorSet_CPU[stage];
			writeDescriptors.dstBinding = VULKAN_DESCRIPTOR_SET_OFFSET_CBV;
			writeDescriptors.dstArrayElement = 0;
			writeDescriptors.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptors.descriptorCount = GPU_RESOURCE_HEAP_CBV_COUNT;
			writeDescriptors.pBufferInfo = bufferInfo;
			writeDescriptors.pImageInfo = nullptr; // Optional
			writeDescriptors.pTexelBufferView = nullptr; // Optional

			vkUpdateDescriptorSets(device->device, 1, &writeDescriptors, 0, nullptr);
		}
	}
	void GraphicsDevice_Vulkan::FrameResources::DescriptorTableFrameAllocator::update(SHADERSTAGE stage, UINT offset, VkBuffer descriptor, VkCommandBuffer commandList)
	{
		//if (descriptor == nullptr)
		//{
		//	return;
		//}
		//UINT idx = stage * itemCount + offset;

		dirty[stage] = true;


		//D3D12_CPU_DESCRIPTOR_HANDLE dst_staging = heap_CPU->GetCPUDescriptorHandleForHeapStart();
		//dst_staging.ptr += idx * itemSize;

		//device->CopyDescriptorsSimple(1, dst_staging, *descriptor, (D3D12_DESCRIPTOR_HEAP_TYPE)descriptorType);
	}
	void GraphicsDevice_Vulkan::FrameResources::DescriptorTableFrameAllocator::validate(VkCommandBuffer commandList)
	{
		for (int stage = 0; stage < SHADERSTAGE_COUNT; ++stage)
		{
			if (dirty[stage])
			{

				// 1.) Copy descriptors from STAGING -> to GPU visible table:

				VkCopyDescriptorSet copyDescriptors = {};

				copyDescriptors.sType = VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
				copyDescriptors.descriptorCount = GPU_RESOURCE_HEAP_CBV_COUNT;

				copyDescriptors.srcSet = descriptorSet_CPU[stage];
				copyDescriptors.srcBinding = VULKAN_DESCRIPTOR_SET_OFFSET_CBV;
				copyDescriptors.srcArrayElement = 0;

				copyDescriptors.dstSet = descriptorSet_GPU[stage][ringOffset[stage]];
				copyDescriptors.dstBinding = VULKAN_DESCRIPTOR_SET_OFFSET_CBV;
				copyDescriptors.dstArrayElement = 0;

				vkUpdateDescriptorSets(device->device, 0, nullptr, 1, &copyDescriptors);


				// 2.) Bind GPU visible descriptor table which we just updated:
				if (stage == CS)
				{
					vkCmdBindDescriptorSets(commandList, VK_PIPELINE_BIND_POINT_COMPUTE, device->defaultPipelineLayout_Compute, 0, 1, &descriptorSet_GPU[stage][ringOffset[stage]], 0, nullptr);
				}
				else
				{
					vkCmdBindDescriptorSets(commandList, VK_PIPELINE_BIND_POINT_GRAPHICS, device->defaultPipelineLayout_Graphics, stage, 1, &descriptorSet_GPU[stage][ringOffset[stage]], 0, nullptr);
				}


				// mark the descriptors of this stage as up to date
				dirty[stage] = false;

				// allocate next chunk for GPU visible descriptor table:
				ringOffset[stage]++;

				if (ringOffset[stage] >= descriptorSet_GPU[stage].size())
				{
					// ran out of descriptor allocation space, stall CPU and wrap the ring buffer:
					assert(0 && "TODO Stall");
					ringOffset[stage] = 0;
				}

			}
		}
	}



	// Converters:
	inline VkFormat _ConvertFormat(FORMAT value)
	{
		// _TYPELESS is converted to _UINT or _FLOAT or _UNORM in that order depending on availability!
		// X channel is converted to regular missing channel (eg. FORMAT_B8G8R8X8_UNORM -> VK_FORMAT_B8G8R8A8_UNORM)
		switch (value)
		{
		case FORMAT_UNKNOWN:
			return VK_FORMAT_UNDEFINED;
			break;
		case FORMAT_R32G32B32A32_TYPELESS:
			return VK_FORMAT_R32G32B32A32_UINT;
			break;
		case FORMAT_R32G32B32A32_FLOAT:
			return VK_FORMAT_R32G32B32A32_SFLOAT;
			break;
		case FORMAT_R32G32B32A32_UINT:
			return VK_FORMAT_R32G32B32A32_UINT;
			break;
		case FORMAT_R32G32B32A32_SINT:
			return VK_FORMAT_R32G32B32A32_SINT;
			break;
		case FORMAT_R32G32B32_TYPELESS:
			return VK_FORMAT_R32G32B32_UINT;
			break;
		case FORMAT_R32G32B32_FLOAT:
			return VK_FORMAT_R32G32B32_SFLOAT;
			break;
		case FORMAT_R32G32B32_UINT:
			return VK_FORMAT_R32G32B32_UINT;
			break;
		case FORMAT_R32G32B32_SINT:
			return VK_FORMAT_R32G32B32_SINT;
			break;
		case FORMAT_R16G16B16A16_TYPELESS:
			return VK_FORMAT_R16G16B16A16_UINT;
			break;
		case FORMAT_R16G16B16A16_FLOAT:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
			break;
		case FORMAT_R16G16B16A16_UNORM:
			return VK_FORMAT_R16G16B16A16_UNORM;
			break;
		case FORMAT_R16G16B16A16_UINT:
			return VK_FORMAT_R16G16B16A16_UINT;
			break;
		case FORMAT_R16G16B16A16_SNORM:
			return VK_FORMAT_R16G16B16A16_SNORM;
			break;
		case FORMAT_R16G16B16A16_SINT:
			return VK_FORMAT_R16G16B16A16_SINT;
			break;
		case FORMAT_R32G32_TYPELESS:
			return VK_FORMAT_R32G32_UINT;
			break;
		case FORMAT_R32G32_FLOAT:
			return VK_FORMAT_R32G32_SFLOAT;
			break;
		case FORMAT_R32G32_UINT:
			return VK_FORMAT_R32G32_UINT;
			break;
		case FORMAT_R32G32_SINT:
			return VK_FORMAT_R32G32_SINT;
			break;
		case FORMAT_R32G8X24_TYPELESS:
			return VK_FORMAT_D32_SFLOAT_S8_UINT; // possible mismatch!
			break;
		case FORMAT_D32_FLOAT_S8X24_UINT:
			return VK_FORMAT_D32_SFLOAT_S8_UINT;
			break;
		case FORMAT_R32_FLOAT_X8X24_TYPELESS:
			return VK_FORMAT_R32G32_SFLOAT; // possible mismatch!
			break;
		case FORMAT_X32_TYPELESS_G8X24_UINT:
			return VK_FORMAT_R32G32_UINT; // possible mismatch!
			break;
		case FORMAT_R10G10B10A2_TYPELESS:
			return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			break;
		case FORMAT_R10G10B10A2_UNORM:
			return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
			break;
		case FORMAT_R10G10B10A2_UINT:
			return VK_FORMAT_A2B10G10R10_UINT_PACK32;
			break;
		case FORMAT_R11G11B10_FLOAT:
			return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
			break;
		case FORMAT_R8G8B8A8_TYPELESS:
			return VK_FORMAT_R8G8B8A8_UINT;
			break;
		case FORMAT_R8G8B8A8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM;
			break;
		case FORMAT_R8G8B8A8_UNORM_SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
			break;
		case FORMAT_R8G8B8A8_UINT:
			return VK_FORMAT_R8G8B8A8_UINT;
			break;
		case FORMAT_R8G8B8A8_SNORM:
			return VK_FORMAT_R8G8B8A8_SNORM;
			break;
		case FORMAT_R8G8B8A8_SINT:
			return VK_FORMAT_R8G8B8A8_SINT;
			break;
		case FORMAT_R16G16_TYPELESS:
			return VK_FORMAT_R16G16_UINT;
			break;
		case FORMAT_R16G16_FLOAT:
			return VK_FORMAT_R16G16_SFLOAT;
			break;
		case FORMAT_R16G16_UNORM:
			return VK_FORMAT_R16G16_UNORM;
			break;
		case FORMAT_R16G16_UINT:
			return VK_FORMAT_R16G16_UINT;
			break;
		case FORMAT_R16G16_SNORM:
			return VK_FORMAT_R16G16_SNORM;
			break;
		case FORMAT_R16G16_SINT:
			return VK_FORMAT_R16G16_SINT;
			break;
		case FORMAT_R32_TYPELESS:
			return VK_FORMAT_D32_SFLOAT;
			break;
		case FORMAT_D32_FLOAT:
			return VK_FORMAT_D32_SFLOAT;
			break;
		case FORMAT_R32_FLOAT:
			return VK_FORMAT_R32_SFLOAT;
			break;
		case FORMAT_R32_UINT:
			return VK_FORMAT_R32_UINT;
			break;
		case FORMAT_R32_SINT:
			return VK_FORMAT_R32_SINT;
			break;
		case FORMAT_R24G8_TYPELESS:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_D24_UNORM_S8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_R24_UNORM_X8_TYPELESS:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_X24_TYPELESS_G8_UINT:
			return VK_FORMAT_D24_UNORM_S8_UINT;
			break;
		case FORMAT_R8G8_TYPELESS:
			return VK_FORMAT_R8G8_UINT;
			break;
		case FORMAT_R8G8_UNORM:
			return VK_FORMAT_R8G8_UNORM;
			break;
		case FORMAT_R8G8_UINT:
			return VK_FORMAT_R8G8_UINT;
			break;
		case FORMAT_R8G8_SNORM:
			return VK_FORMAT_R8G8_SNORM;
			break;
		case FORMAT_R8G8_SINT:
			return VK_FORMAT_R8G8_SINT;
			break;
		case FORMAT_R16_TYPELESS:
			return VK_FORMAT_D16_UNORM;
			break;
		case FORMAT_R16_FLOAT:
			return VK_FORMAT_R16_SFLOAT;
			break;
		case FORMAT_D16_UNORM:
			return VK_FORMAT_D16_UNORM;
			break;
		case FORMAT_R16_UNORM:
			return VK_FORMAT_R16_UNORM;
			break;
		case FORMAT_R16_UINT:
			return VK_FORMAT_R16_UINT;
			break;
		case FORMAT_R16_SNORM:
			return VK_FORMAT_R16_SNORM;
			break;
		case FORMAT_R16_SINT:
			return VK_FORMAT_R16_SINT;
			break;
		case FORMAT_R8_TYPELESS:
			return VK_FORMAT_R8_UINT;
			break;
		case FORMAT_R8_UNORM:
			return VK_FORMAT_R8_UNORM;
			break;
		case FORMAT_R8_UINT:
			return VK_FORMAT_R8_UINT;
			break;
		case FORMAT_R8_SNORM:
			return VK_FORMAT_R8_SNORM;
			break;
		case FORMAT_R8_SINT:
			return VK_FORMAT_R8_SINT;
			break;
		case FORMAT_A8_UNORM:
			return VK_FORMAT_R8_UNORM; // mismatch!
			break;
		case FORMAT_R1_UNORM:
			return VK_FORMAT_R8_UNORM; // mismatch!
			break;
		case FORMAT_R9G9B9E5_SHAREDEXP:
			return VK_FORMAT_E5B9G9R9_UFLOAT_PACK32; // maybe ok
			break;
		case FORMAT_R8G8_B8G8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM; // mismatch
			break;
		case FORMAT_G8R8_G8B8_UNORM:
			return VK_FORMAT_R8G8B8A8_UNORM; // mismatch
			break;
		case FORMAT_BC1_TYPELESS:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			break;
		case FORMAT_BC1_UNORM:
			return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
			break;
		case FORMAT_BC1_UNORM_SRGB:
			return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
			break;
		case FORMAT_BC2_TYPELESS:
			return VK_FORMAT_BC2_UNORM_BLOCK;
			break;
		case FORMAT_BC2_UNORM:
			return VK_FORMAT_BC2_UNORM_BLOCK;
			break;
		case FORMAT_BC2_UNORM_SRGB:
			return VK_FORMAT_BC2_SRGB_BLOCK;
			break;
		case FORMAT_BC3_TYPELESS:
			return VK_FORMAT_BC3_UNORM_BLOCK;
			break;
		case FORMAT_BC3_UNORM:
			return VK_FORMAT_BC3_UNORM_BLOCK;
			break;
		case FORMAT_BC3_UNORM_SRGB:
			return VK_FORMAT_BC3_SRGB_BLOCK;
			break;
		case FORMAT_BC4_TYPELESS:
			return VK_FORMAT_BC4_UNORM_BLOCK;
			break;
		case FORMAT_BC4_UNORM:
			return VK_FORMAT_BC4_UNORM_BLOCK;
			break;
		case FORMAT_BC4_SNORM:
			return VK_FORMAT_BC4_SNORM_BLOCK;
			break;
		case FORMAT_BC5_TYPELESS:
			return VK_FORMAT_BC5_UNORM_BLOCK;
			break;
		case FORMAT_BC5_UNORM:
			return VK_FORMAT_BC5_UNORM_BLOCK;
			break;
		case FORMAT_BC5_SNORM:
			return VK_FORMAT_BC5_SNORM_BLOCK;
			break;
		case FORMAT_B5G6R5_UNORM:
			return VK_FORMAT_B5G6R5_UNORM_PACK16;
			break;
		case FORMAT_B5G5R5A1_UNORM:
			return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
			break;
		case FORMAT_B8G8R8A8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
			break;
		case FORMAT_B8G8R8X8_UNORM:
			return VK_FORMAT_B8G8R8A8_UNORM;
			break;
		case FORMAT_R10G10B10_XR_BIAS_A2_UNORM:
			return VK_FORMAT_B10G11R11_UFLOAT_PACK32; // mismatch
			break;
		case FORMAT_B8G8R8A8_TYPELESS:
			return VK_FORMAT_B8G8R8A8_UINT;
			break;
		case FORMAT_B8G8R8A8_UNORM_SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
			break;
		case FORMAT_B8G8R8X8_TYPELESS:
			return VK_FORMAT_B8G8R8A8_UINT;
			break;
		case FORMAT_B8G8R8X8_UNORM_SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
			break;
		case FORMAT_BC6H_TYPELESS:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			break;
		case FORMAT_BC6H_UF16:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
			break;
		case FORMAT_BC6H_SF16:
			return VK_FORMAT_BC6H_SFLOAT_BLOCK;
			break;
		case FORMAT_BC7_TYPELESS:
			return VK_FORMAT_BC7_UNORM_BLOCK; // maybe mismatch
			break;
		case FORMAT_BC7_UNORM:
			return VK_FORMAT_BC7_UNORM_BLOCK;
			break;
		case FORMAT_BC7_UNORM_SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;
			break;
		case FORMAT_B4G4R4A4_UNORM:
			return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
			break;
		default:
			break;
		}
		return VK_FORMAT_UNDEFINED;
	}


	// Engine functions
	VkCommandBuffer GraphicsDevice_Vulkan::GetDirectCommandList(GRAPHICSTHREAD threadID) { return GetFrameResources().commandBuffers[threadID]; }

	GraphicsDevice_Vulkan::GraphicsDevice_Vulkan(wiWindowRegistration::window_type window, bool fullscreen) : GraphicsDevice()
	{
		FULLSCREEN = fullscreen;

		RECT rect = RECT();
		GetClientRect(window, &rect);
		SCREENWIDTH = rect.right - rect.left;
		SCREENHEIGHT = rect.bottom - rect.top;



		// Fill out application info:
		VkApplicationInfo appInfo = {};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Wicked Engine Application";
		appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.pEngineName = "Wicked Engine";
		appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		appInfo.apiVersion = VK_API_VERSION_1_0;

		// Enumerate available extensions:
		uint32_t extensionCount = 0;
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> extensions(extensionCount);
		vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

		std::vector<const char*> extensionNames;
		//for (auto& x : extensions)
		//{
		//	extensionNames.push_back(x.extensionName);
		//}
		extensionNames.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
		extensionNames.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
		
		if (enableValidationLayers && !checkValidationLayerSupport()) {
			//throw std::runtime_error("validation layers requested, but not available!");
			wiHelper::messageBox("Vulkan validation layer requested but not available!");
			enableValidationLayers = false;
		}
		else if (enableValidationLayers)
		{
			extensionNames.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
		}

		// Create instance:
		{
			VkInstanceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			createInfo.pApplicationInfo = &appInfo;
			createInfo.enabledExtensionCount = static_cast<uint32_t>(extensionNames.size());
			createInfo.ppEnabledExtensionNames = extensionNames.data();
			createInfo.enabledLayerCount = 0;
			if (enableValidationLayers)
			{
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();
			}
			if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
				throw std::runtime_error("failed to create instance!");
			}
		}

		// Register validation layer callback:
		if (enableValidationLayers)
		{
			VkDebugReportCallbackCreateInfoEXT createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			createInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
			createInfo.pfnCallback = debugCallback;
			if (CreateDebugReportCallbackEXT(instance, &createInfo, nullptr, &callback) != VK_SUCCESS) {
				throw std::runtime_error("failed to set up debug callback!");
			}
		}


		// Surface creation:
		{
#ifdef _WIN32
			VkWin32SurfaceCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
			createInfo.hwnd = window;
			createInfo.hinstance = GetModuleHandle(nullptr);

			auto CreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR)vkGetInstanceProcAddr(instance, "vkCreateWin32SurfaceKHR");

			if (!CreateWin32SurfaceKHR || CreateWin32SurfaceKHR(instance, &createInfo, nullptr, &surface) != VK_SUCCESS) {
				throw std::runtime_error("failed to create window surface!");
			}
#else
#error WICKEDENGINE VULKAN DEVICE ERROR: PLATFORM NOT SUPPORTED
#endif // WIN32
		}


		// Enumerating and creating devices:
		{
			uint32_t deviceCount = 0;
			vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

			if (deviceCount == 0) {
				throw std::runtime_error("failed to find GPUs with Vulkan support!");
			}

			std::vector<VkPhysicalDevice> devices(deviceCount);
			vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

			for (const auto& device : devices) {
				if (isDeviceSuitable(device, surface)) {
					physicalDevice = device;
					break;
				}
			}

			if (physicalDevice == VK_NULL_HANDLE) {
				throw std::runtime_error("failed to find a suitable GPU!");
			}

			QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
			std::set<int> uniqueQueueFamilies = { indices.graphicsFamily, indices.presentFamily };

			float queuePriority = 1.0f;
			for (int queueFamily : uniqueQueueFamilies) {
				VkDeviceQueueCreateInfo queueCreateInfo = {};
				queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueCreateInfo.queueFamilyIndex = queueFamily;
				queueCreateInfo.queueCount = 1;
				queueCreateInfo.pQueuePriorities = &queuePriority;
				queueCreateInfos.push_back(queueCreateInfo);
			}

			VkPhysicalDeviceFeatures deviceFeatures = {};
			vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);

			VkDeviceCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

			createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			createInfo.pQueueCreateInfos = queueCreateInfos.data();

			createInfo.pEnabledFeatures = &deviceFeatures;

			createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
			createInfo.ppEnabledExtensionNames = deviceExtensions.data();

			if (enableValidationLayers) {
				createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
				createInfo.ppEnabledLayerNames = validationLayers.data();
			}
			else {
				createInfo.enabledLayerCount = 0;
			}

			if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
				throw std::runtime_error("failed to create logical device!");
			}

			vkGetDeviceQueue(device, indices.graphicsFamily, 0, &graphicsQueue);
			vkGetDeviceQueue(device, indices.presentFamily, 0, &presentQueue);
			vkGetDeviceQueue(device, indices.copyFamily, 0, &copyQueue);
		}


		// Create default pipeline:
		{

			//
			//								##################################################################################
			//								##		The desired descriptor layout will be as such (per shader stage)		##
			//								##################################################################################
			//
			//	- We are mapping HLSL constructs to Vulkan descriptor type equivalents. The difference is that DX11 manages resource bindings by "Memory Type"
			//		but HLSL has distinctive resource types which map to them. Vulkan API has a more straight forward mapping but we are emulating the
			//		DX11 system for now...
			//
			//	- We are creating this table (descriptor set) for every shader stage. The SPIR-V shaders will have set and layout bindings compiled
			//		into them for each resource. 
			//			- The [layout set] binding will correspond to shader stage
			//				- except in compute shader because it will have only single descriptor table, special logic will handle that
			//			- The [layout location] binding will correspond to Vulkan name offset inside the set which is hard coded 
			//				(eg. see VULKAN_DESCRIPTOR_SET_OFFSET_CBV in ShaderInterop_Vulkan.h)
			//
			//	- Left hand side of this table is essentially DX12-like descriptor table layout (per stage)
			//		- DX12 maps perfectly to DX11 regarding table layout
			//	- Right hand side is corresponding Vulkan layout (per stage).
			//		- Vulkan implementation has bigger tables. 
			//			- CBV table has same amount like DX12
			//			- SRV table has 3x amount of DX12
			//			- UAV table has 3x amount of DX12
			//				- UAV counter buffer would take +1x but not used for now...
			//			- Sampler table has same amount like DX12
			//
			//	================================================================================||===============================================================
			//	|	DX11 Memory Type	|	Slot	|	HLSL name								||	Vulkan name				|	Descriptor count				|
			//	|===============================================================================||==============================================================|
			//	|	ImmediateIndexable	|	b		|	cbuffer, ConstantBuffer					||	Uniform Buffer			|	GPU_RESOURCE_HEAP_CBV_COUNT		|
			//	|-----------------------|-----------|-------------------------------------------||--------------------------|-----------------------------------|
			//	|	ShaderResourceView	|	t		|	Texture									||	Sampled Image			|	GPU_RESOURCE_HEAP_SRV_COUNT		|
			//	|						|			|	Buffer									||	Uniform Texel Buffer	|	GPU_RESOURCE_HEAP_SRV_COUNT		|
			//	|						|			|	StructuredBuffer, ByteAddressBuffer		||	Storage Buffer			|	GPU_RESOURCE_HEAP_SRV_COUNT		|
			//	|-----------------------|-----------|-------------------------------------------||--------------------------|-----------------------------------|
			//	|	UnorderedAccessView	|	u		|	RWTexture								||	Storage Image			|	GPU_RESOURCE_HEAP_UAV_COUNT		|
			//	|						|			|	RWBuffer								||	Storage Texel Buffer	|	GPU_RESOURCE_HEAP_UAV_COUNT		|
			//	|						|			|	RWStructuredBuffer, RWByteAddressBuffer	||	Storage Buffer			|	GPU_RESOURCE_HEAP_UAV_COUNT		|
			//	|-----------------------|-----------|-------------------------------------------||--------------------------|-----------------------------------|
			//	|	Sampler				|	s		|	SamplerState							||	Sampler					|	GPU_SAMPLER_HEAP_COUNT			|
			//	================================================================================||===============================================================
			//

			std::vector<VkDescriptorSetLayoutBinding> layoutBindings = {};

			int offset = 0;

			// NOTE: we will create the layoutBinding beforehand, but only change the shader stage binding later:

			// Constant Buffers:
			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_CBV);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_CBV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}

			// Shader Resource Views:
			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_SRV_TEXTURE);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_SRV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}

			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_SRV_TYPEDBUFFER);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_SRV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}

			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_SRV_UNTYPEDBUFFER);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_SRV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}


			// Unordered Access Views:
			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_UAV_TEXTURE);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_UAV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}

			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_UAV_TYPEDBUFFER);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_UAV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}

			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_UAV_UNTYPEDBUFFER);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_RESOURCE_HEAP_UAV_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}


			// Samplers:
			assert(offset == VULKAN_DESCRIPTOR_SET_OFFSET_SAMPLER);
			{
				VkDescriptorSetLayoutBinding layoutBinding = {};
				layoutBinding.stageFlags = 0;
				layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
				layoutBinding.binding = offset;
				layoutBinding.descriptorCount = GPU_SAMPLER_HEAP_COUNT;
				layoutBindings.push_back(layoutBinding);

				offset += layoutBinding.descriptorCount;
			}

			descriptorCount = offset;


			for (int stage = 0; stage < SHADERSTAGE_COUNT; ++stage)
			{
				VkShaderStageFlags vkstage;

				switch (stage)
				{
				case VS:
					vkstage = VK_SHADER_STAGE_VERTEX_BIT;
					break;
				case GS:
					vkstage = VK_SHADER_STAGE_GEOMETRY_BIT;
					break;
				case HS:
					vkstage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
					break;
				case DS:
					vkstage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
					break;
				case PS:
					vkstage = VK_SHADER_STAGE_FRAGMENT_BIT;
					break;
				case CS:
					vkstage = VK_SHADER_STAGE_COMPUTE_BIT;
					break;
				}

				// all stages will have the same layout, just different shader stage visibility:
				for (auto& x : layoutBindings)
				{
					x.stageFlags = vkstage;
				}

				VkDescriptorSetLayoutCreateInfo descriptorSetlayoutInfo = {};
				descriptorSetlayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
				descriptorSetlayoutInfo.pBindings = layoutBindings.data();
				descriptorSetlayoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
				if (vkCreateDescriptorSetLayout(device, &descriptorSetlayoutInfo, nullptr, &defaultDescriptorSetlayouts[stage]) != VK_SUCCESS) {
					throw std::runtime_error("failed to create descriptor set layout!");
				}
			}

			// Graphics:
			{
				VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
				pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipelineLayoutInfo.pSetLayouts = defaultDescriptorSetlayouts;
				pipelineLayoutInfo.setLayoutCount = 5; // vs, gs, hs, ds, ps
				pipelineLayoutInfo.pushConstantRangeCount = 0;

				if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &defaultPipelineLayout_Graphics) != VK_SUCCESS) {
					throw std::runtime_error("failed to create graphics pipeline layout!");
				}
			}

			// Compute:
			{
				VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
				pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
				pipelineLayoutInfo.pSetLayouts = &defaultDescriptorSetlayouts[CS];
				pipelineLayoutInfo.setLayoutCount = 1; // cs
				pipelineLayoutInfo.pushConstantRangeCount = 0;

				if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &defaultPipelineLayout_Compute) != VK_SUCCESS) {
					throw std::runtime_error("failed to create compute pipeline layout!");
				}
			}
		}


		// Set up swap chain:
		{
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice, surface);

			VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
			VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);

			swapChainExtent = { static_cast<uint32_t>(SCREENWIDTH), static_cast<uint32_t>(SCREENHEIGHT) };
			swapChainExtent.width = max(swapChainSupport.capabilities.minImageExtent.width, min(swapChainSupport.capabilities.maxImageExtent.width, swapChainExtent.width));
			swapChainExtent.height = max(swapChainSupport.capabilities.minImageExtent.height, min(swapChainSupport.capabilities.maxImageExtent.height, swapChainExtent.height));

			//uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
			//if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
			//	imageCount = swapChainSupport.capabilities.maxImageCount;
			//}

			uint32_t imageCount = BACKBUFFER_COUNT;

			VkSwapchainCreateInfoKHR createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			createInfo.surface = surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = surfaceFormat.format;
			createInfo.imageColorSpace = surfaceFormat.colorSpace;
			createInfo.imageExtent = swapChainExtent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);
			uint32_t queueFamilyIndices[] = { (uint32_t)indices.graphicsFamily, (uint32_t)indices.presentFamily };

			if (indices.graphicsFamily != indices.presentFamily) {
				createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				createInfo.queueFamilyIndexCount = 2;
				createInfo.pQueueFamilyIndices = queueFamilyIndices;
			}
			else {
				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				createInfo.queueFamilyIndexCount = 0; // Optional
				createInfo.pQueueFamilyIndices = nullptr; // Optional
			}

			createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = VK_NULL_HANDLE;

			if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
				throw std::runtime_error("failed to create swap chain!");
			}

			vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
			assert(imageCount == BACKBUFFER_COUNT);
			swapChainImages.resize(imageCount);
			vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
			swapChainImageFormat = surfaceFormat.format;

		}




		// Create default render pass:
		{
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = swapChainImageFormat;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			renderPassInfo.dependencyCount = 1;
			renderPassInfo.pDependencies = &dependency;

			if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &defaultRenderPass) != VK_SUCCESS) {
				throw std::runtime_error("failed to create render pass!");
			}

			memset(renderPassActive, 0, sizeof(renderPassActive));
			memset(attachments, 0, sizeof(attachments));
		}

		// Create frame resources:
		{
			int i = 0;
			for(auto& frame : frames)
			{
				// Fence:
				{
					VkFenceCreateInfo fenceInfo = {};
					fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
					//fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
					vkCreateFence(device, &fenceInfo, nullptr, &frame.frameFence);
				}

				// Create swap chain render targets:
				{
					VkImageViewCreateInfo createInfo = {};
					createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					createInfo.image = swapChainImages[i];
					createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
					createInfo.format = swapChainImageFormat;
					createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
					createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
					createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
					createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
					createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					createInfo.subresourceRange.baseMipLevel = 0;
					createInfo.subresourceRange.levelCount = 1;
					createInfo.subresourceRange.baseArrayLayer = 0;
					createInfo.subresourceRange.layerCount = 1;

					if (vkCreateImageView(device, &createInfo, nullptr, &frame.swapChainImageView) != VK_SUCCESS) {
						throw std::runtime_error("failed to create image views!");
					}

					VkImageView attachments[] = {
						frame.swapChainImageView
					};

					VkFramebufferCreateInfo framebufferInfo = {};
					framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
					framebufferInfo.renderPass = defaultRenderPass;
					framebufferInfo.attachmentCount = 1;
					framebufferInfo.pAttachments = attachments;
					framebufferInfo.width = swapChainExtent.width;
					framebufferInfo.height = swapChainExtent.height;
					framebufferInfo.layers = 1;

					if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frame.swapChainFramebuffer) != VK_SUCCESS) {
						throw std::runtime_error("failed to create framebuffer!");
					}
				}

				// Create command buffers:
				{
					QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, surface);

					for (int threadID = 0; threadID < GRAPHICSTHREAD_COUNT; ++threadID)
					{
						VkCommandPoolCreateInfo poolInfo = {};
						poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
						poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
						poolInfo.flags = 0; // Optional

						if (vkCreateCommandPool(device, &poolInfo, nullptr, &frame.commandPools[threadID]) != VK_SUCCESS) {
							throw std::runtime_error("failed to create command pool!");
						}

						VkCommandBufferAllocateInfo commandBufferInfo = {};
						commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
						commandBufferInfo.commandBufferCount = 1;
						commandBufferInfo.commandPool = frame.commandPools[threadID];
						commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

						if (vkAllocateCommandBuffers(device, &commandBufferInfo, &frame.commandBuffers[threadID]) != VK_SUCCESS) {
							throw std::runtime_error("failed to create command buffers!");
						}

						VkCommandBufferBeginInfo beginInfo = {};
						beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
						beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
						beginInfo.pInheritanceInfo = nullptr; // Optional

						VkResult res = vkBeginCommandBuffer(frame.commandBuffers[threadID], &beginInfo);
						assert(res == VK_SUCCESS);
					}
				}


				// Create immediate resource allocators:
				for (int threadID = 0; threadID < GRAPHICSTHREAD_COUNT; ++threadID)
				{
					frame.ResourceDescriptorsGPU[threadID] = new FrameResources::DescriptorTableFrameAllocator(this, 1024);
					frame.resourceBuffer[threadID] = new FrameResources::ResourceFrameAllocator(physicalDevice, device, 4 * 1024 * 1024);
				}


				i++;
			}
		}

		// Create semaphores:
		{
			VkSemaphoreCreateInfo semaphoreInfo = {};
			semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

			if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
				vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS) {

				throw std::runtime_error("failed to create semaphores!");
			}
		}


		// Create resources for copy (transfer) queue:
		{
			QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, surface); // redundant!!


			VkCommandPoolCreateInfo poolInfo = {};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = queueFamilyIndices.copyFamily;
			poolInfo.flags = 0; // Optional

			if (vkCreateCommandPool(device, &poolInfo, nullptr, &copyCommandPool) != VK_SUCCESS) {
				throw std::runtime_error("failed to create command pool!");
			}

			VkCommandBufferAllocateInfo commandBufferInfo = {};
			commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			commandBufferInfo.commandBufferCount = 1;
			commandBufferInfo.commandPool = copyCommandPool;
			commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

			if (vkAllocateCommandBuffers(device, &commandBufferInfo, &copyCommandBuffer) != VK_SUCCESS) {
				throw std::runtime_error("failed to create command buffers!");
			}

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			beginInfo.pInheritanceInfo = nullptr; // Optional

			VkResult res = vkBeginCommandBuffer(copyCommandBuffer, &beginInfo);
			assert(res == VK_SUCCESS);


			// Fence for copy queue:
			VkFenceCreateInfo fenceInfo = {};
			fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			//fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
			vkCreateFence(device, &fenceInfo, nullptr, &copyFence);
		}


		// Create resource upload buffers
		bufferUploader = new UploadBuffer(physicalDevice, device, 256 * 1024 * 1024);
		textureUploader = new UploadBuffer(physicalDevice, device, 256 * 1024 * 1024);


		// Create default null descriptors:
		{
			VkBufferCreateInfo bufferInfo = {};
			bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferInfo.size = 4;
			bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			bufferInfo.flags = 0;

			VkResult res = vkCreateBuffer(device, &bufferInfo, nullptr, &nullBuffer);
			assert(res == VK_SUCCESS);


			// Allocate resource backing memory:
			VkMemoryRequirements memRequirements;
			vkGetBufferMemoryRequirements(device, nullBuffer, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkDeviceMemory mem;
			if (vkAllocateMemory(device, &allocInfo, nullptr, &mem) != VK_SUCCESS) {
				throw std::runtime_error("failed to allocate buffer memory!");
			}

			res = vkBindBufferMemory(device, nullBuffer, mem, 0);
			assert(res == VK_SUCCESS);
		}
		{
			VkImageCreateInfo imageInfo = {};
			imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageInfo.imageType = VK_IMAGE_TYPE_2D;
			imageInfo.extent.width = 1;
			imageInfo.extent.height = 1;
			imageInfo.extent.depth = 1;
			imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
			imageInfo.arrayLayers = 1;
			imageInfo.mipLevels = 1;
			imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageInfo.usage = 0;
			imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
			imageInfo.flags = 0;

			VkResult res = vkCreateImage(device, &imageInfo, nullptr, &nullImage);
			assert(res == VK_SUCCESS);


			// Allocate resource backing memory:
			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(device, nullImage, &memRequirements);

			VkMemoryAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

			VkDeviceMemory mem;
			if (vkAllocateMemory(device, &allocInfo, nullptr, &mem) != VK_SUCCESS) {
				throw std::runtime_error("failed to allocate image memory!");
			}

			res = vkBindImageMemory(device, nullImage, mem, 0);
			assert(res == VK_SUCCESS);
		}
		{
			VkSamplerCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

			VkResult res = vkCreateSampler(device, &createInfo, nullptr, &nullSampler);
			assert(res == VK_SUCCESS);
		}


		// Preinitialize staging descriptor tables:
		for (auto& frame : frames)
		{
			for (int threadID = 0; threadID < GRAPHICSTHREAD_COUNT; ++threadID)
			{
				frame.ResourceDescriptorsGPU[threadID]->reset();
			}
		}

	}
	GraphicsDevice_Vulkan::~GraphicsDevice_Vulkan()
	{
		WaitForGPU();

		SAFE_DELETE(bufferUploader);
		SAFE_DELETE(textureUploader);

		for (auto& frame : frames)
		{
			vkDestroyFence(device, frame.frameFence, nullptr);
			vkDestroyFramebuffer(device, frame.swapChainFramebuffer, nullptr);
			vkDestroyImageView(device, frame.swapChainImageView, nullptr);
			for (auto& commandPool : frame.commandPools)
			{
				vkDestroyCommandPool(device, commandPool, nullptr);
			}

			for (int threadID = 0; threadID < GRAPHICSTHREAD_COUNT; ++threadID)
			{
				SAFE_DELETE(frame.ResourceDescriptorsGPU[threadID]);
				SAFE_DELETE(frame.resourceBuffer[threadID]);
			}
		}

		vkDestroySemaphore(device, renderFinishedSemaphore, nullptr);
		vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);

		for (int i = 0; i < SHADERSTAGE_COUNT; ++i)
		{
			vkDestroyDescriptorSetLayout(device, defaultDescriptorSetlayouts[i], nullptr);
		}
		vkDestroyPipelineLayout(device, defaultPipelineLayout_Graphics, nullptr);
		vkDestroyPipelineLayout(device, defaultPipelineLayout_Compute, nullptr);
		vkDestroyRenderPass(device, defaultRenderPass, nullptr);

		for (auto& x : swapChainImages)
		{
			vkDestroyImage(device, x, nullptr);
		}
		vkDestroySwapchainKHR(device, swapChain, nullptr);
		vkDestroyDevice(device, nullptr);
		DestroyDebugReportCallbackEXT(instance, callback, nullptr);
		vkDestroyInstance(instance, nullptr);
	}

	void GraphicsDevice_Vulkan::SetResolution(int width, int height)
	{
		if (width != SCREENWIDTH || height != SCREENHEIGHT)
		{
			SCREENWIDTH = width;
			SCREENHEIGHT = height;
			//swapChain->ResizeBuffers(2, width, height, _ConvertFormat(GetBackBufferFormat()), 0);
			RESOLUTIONCHANGED = true;
		}
	}

	Texture2D GraphicsDevice_Vulkan::GetBackBuffer()
	{
		return Texture2D();
	}

	HRESULT GraphicsDevice_Vulkan::CreateBuffer(const GPUBufferDesc *pDesc, const SubresourceData* pInitialData, GPUBuffer *ppBuffer)
	{
		HRESULT hr = E_FAIL;

		ppBuffer->desc = *pDesc;

		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.size = ppBuffer->desc.ByteWidth;
		bufferInfo.usage = 0;
		if (ppBuffer->desc.BindFlags & BIND_VERTEX_BUFFER)
		{
			bufferInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		}
		if (ppBuffer->desc.BindFlags & BIND_INDEX_BUFFER)
		{
			bufferInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		}
		if (ppBuffer->desc.BindFlags & BIND_CONSTANT_BUFFER)
		{
			bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		}
		if (ppBuffer->desc.BindFlags & BIND_SHADER_RESOURCE)
		{
			if (ppBuffer->desc.Format == FORMAT_UNKNOWN)
			{
				bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			}
			else
			{
				bufferInfo.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
			}
		}
		if (ppBuffer->desc.BindFlags & BIND_UNORDERED_ACCESS)
		{
			if (ppBuffer->desc.Format == FORMAT_UNKNOWN)
			{
				bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			}
			else
			{
				bufferInfo.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
			}
		}

		bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

		bufferInfo.flags = 0;

		VkResult res;
		res = vkCreateBuffer(device, &bufferInfo, nullptr, reinterpret_cast<VkBuffer*>(&ppBuffer->resource_Vulkan));
		hr = res == VK_SUCCESS;
		assert(SUCCEEDED(hr));



		// Allocate resource backing memory:
		VkMemoryRequirements memRequirements;
		vkGetBufferMemoryRequirements(device, static_cast<VkBuffer>(ppBuffer->resource_Vulkan), &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		if (vkAllocateMemory(device, &allocInfo, nullptr, reinterpret_cast<VkDeviceMemory*>(&ppBuffer->resourceMemory_Vulkan)) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate buffer memory!");
		}

		res = vkBindBufferMemory(device, static_cast<VkBuffer>(ppBuffer->resource_Vulkan), reinterpret_cast<VkDeviceMemory>(ppBuffer->resourceMemory_Vulkan), 0);
		hr = res == VK_SUCCESS;
		assert(SUCCEEDED(hr));



		// Issue data copy on request:
		if (pInitialData != nullptr)
		{
			uint8_t* dest = bufferUploader->allocate(pDesc->ByteWidth, 4);
			memcpy(dest, pInitialData->pSysMem, pDesc->ByteWidth);

			VkBufferCopy copyRegion = {};
			copyRegion.size = pDesc->ByteWidth;
			copyRegion.srcOffset = 0;
			copyRegion.dstOffset = bufferUploader->calculateOffset(dest);

			copyQueueLock.lock();
			vkCmdCopyBuffer(copyCommandBuffer, bufferUploader->resource, static_cast<VkBuffer>(ppBuffer->resource_Vulkan), 1, &copyRegion);
			copyQueueLock.unlock();
		}



		if (pDesc->BindFlags & BIND_SHADER_RESOURCE && ppBuffer->desc.Format != FORMAT_UNKNOWN)
		{
			VkBufferViewCreateInfo srv_desc = {};
			srv_desc.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
			srv_desc.buffer = reinterpret_cast<VkBuffer>(ppBuffer->resource_Vulkan);
			srv_desc.flags = 0;
			srv_desc.format = _ConvertFormat(ppBuffer->desc.Format);
			srv_desc.offset = 0;
			srv_desc.range = ppBuffer->desc.ByteWidth;

			res = vkCreateBufferView(device, &srv_desc, nullptr, reinterpret_cast<VkBufferView*>(&ppBuffer->SRV_Vulkan));
			assert(res == VK_SUCCESS);
		}

		if (pDesc->BindFlags & BIND_UNORDERED_ACCESS && ppBuffer->desc.Format != FORMAT_UNKNOWN)
		{
			VkBufferViewCreateInfo uav_desc = {};
			uav_desc.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
			uav_desc.buffer = static_cast<VkBuffer>(ppBuffer->resource_Vulkan);
			uav_desc.flags = 0;
			uav_desc.format = _ConvertFormat(ppBuffer->desc.Format);
			uav_desc.offset = 0;
			uav_desc.range = ppBuffer->desc.ByteWidth;

			res = vkCreateBufferView(device, &uav_desc, nullptr, reinterpret_cast<VkBufferView*>(&ppBuffer->UAV_Vulkan));
			assert(res == VK_SUCCESS);
		}



		return hr;
	}
	HRESULT GraphicsDevice_Vulkan::CreateTexture1D(const Texture1DDesc* pDesc, const SubresourceData *pInitialData, Texture1D **ppTexture1D)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateTexture2D(const Texture2DDesc* pDesc, const SubresourceData *pInitialData, Texture2D **ppTexture2D)
	{
		if ((*ppTexture2D) == nullptr)
		{
			(*ppTexture2D) = new Texture2D;
		}
		(*ppTexture2D)->desc = *pDesc;

		if ((*ppTexture2D)->desc.MipLevels == 0)
		{
			(*ppTexture2D)->desc.MipLevels = static_cast<UINT>(log2(max((*ppTexture2D)->desc.Width, (*ppTexture2D)->desc.Height)));
		}


		HRESULT hr = E_FAIL;

		VkImageCreateInfo imageInfo = {};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = (*ppTexture2D)->desc.Width;
		imageInfo.extent.height = (*ppTexture2D)->desc.Height;
		imageInfo.extent.depth = 1;
		imageInfo.format = _ConvertFormat((*ppTexture2D)->desc.Format);
		imageInfo.arrayLayers = (*ppTexture2D)->desc.ArraySize;
		imageInfo.mipLevels = (*ppTexture2D)->desc.MipLevels;
		imageInfo.samples = static_cast<VkSampleCountFlagBits>((*ppTexture2D)->desc.SampleDesc.Count);
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // or preinitialized?
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = 0;
		if ((*ppTexture2D)->desc.BindFlags & BIND_SHADER_RESOURCE)
		{
			imageInfo.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if ((*ppTexture2D)->desc.BindFlags & BIND_RENDER_TARGET)
		{
			imageInfo.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if ((*ppTexture2D)->desc.BindFlags & BIND_DEPTH_STENCIL)
		{
			imageInfo.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}
		if (pInitialData != nullptr)
		{
			imageInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		imageInfo.flags = 0;
		if ((*ppTexture2D)->desc.MiscFlags & RESOURCE_MISC_TEXTURECUBE)
		{
			imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}

		VkResult res;
		res = vkCreateImage(device, &imageInfo, nullptr, reinterpret_cast<VkImage*>(&(*ppTexture2D)->resource_Vulkan));
		hr = res == VK_SUCCESS;
		assert(SUCCEEDED(hr));



		// Allocate resource backing memory:
		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(device, static_cast<VkImage>((*ppTexture2D)->resource_Vulkan), &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		if (vkAllocateMemory(device, &allocInfo, nullptr, reinterpret_cast<VkDeviceMemory*>(&(*ppTexture2D)->resourceMemory_Vulkan)) != VK_SUCCESS) {
			throw std::runtime_error("failed to allocate image memory!");
		}

		res = vkBindImageMemory(device, static_cast<VkImage>((*ppTexture2D)->resource_Vulkan), static_cast<VkDeviceMemory>((*ppTexture2D)->resourceMemory_Vulkan), 0);
		hr = res == VK_SUCCESS; 
		assert(SUCCEEDED(hr));



		// Issue data copy on request:
		if (pInitialData != nullptr)
		{
			uint8_t* dest = textureUploader->allocate(memRequirements.size, memRequirements.alignment);

			VkBufferImageCopy copyRegions[16] = {};
			assert(pDesc->ArraySize < 16);

			uint32_t width = pDesc->Width;
			uint32_t height = pDesc->Height;
			for (UINT slice = 0; slice < pDesc->ArraySize; ++slice)
			{
				memcpy(dest, pInitialData[slice].pSysMem, pInitialData[slice].SysMemPitch * height);  // double check!!

				VkBufferImageCopy& copyRegion = copyRegions[slice];
				copyRegion.bufferOffset = 0;
				copyRegion.bufferRowLength = 0;
				copyRegion.bufferImageHeight = 0;

				// for now, only mips can be filled like this:
				copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.imageSubresource.mipLevel = slice;
				copyRegion.imageSubresource.baseArrayLayer = 0;
				copyRegion.imageSubresource.layerCount = 1;

				copyRegion.imageOffset = { 0, 0, 0 };
				copyRegion.imageExtent = {
					width,
					height,
					1
				};

				width  = max(1, width / 2);
				height /= max(1, height / 2);
			}

			copyQueueLock.lock();
			vkCmdCopyBufferToImage(copyCommandBuffer, textureUploader->resource, static_cast<VkImage>((*ppTexture2D)->resource_Vulkan), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, pDesc->ArraySize, copyRegions);
			copyQueueLock.unlock();

		}



		// Issue creation of additional descriptors for the resource:

		if ((*ppTexture2D)->desc.BindFlags & BIND_RENDER_TARGET)
		{
		}


		if ((*ppTexture2D)->desc.BindFlags & BIND_DEPTH_STENCIL)
		{
		}


		if ((*ppTexture2D)->desc.BindFlags & BIND_SHADER_RESOURCE || (*ppTexture2D)->desc.BindFlags & BIND_RENDER_TARGET || (*ppTexture2D)->desc.BindFlags & BIND_DEPTH_STENCIL)
		{
			UINT arraySize = (*ppTexture2D)->desc.ArraySize;
			UINT sampleCount = (*ppTexture2D)->desc.SampleDesc.Count;
			bool multisampled = sampleCount > 1;

			VkImageViewCreateInfo srv_desc = {};
			srv_desc.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			srv_desc.flags = 0;
			srv_desc.image = static_cast<VkImage>((*ppTexture2D)->resource_Vulkan);
			srv_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;
			srv_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

			if ((*ppTexture2D)->desc.BindFlags & BIND_DEPTH_STENCIL)
			{
				srv_desc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			}

			srv_desc.subresourceRange.baseArrayLayer = 0;
			srv_desc.subresourceRange.layerCount = 1;
			srv_desc.subresourceRange.baseMipLevel = 0;
			srv_desc.subresourceRange.levelCount = 1;

			srv_desc.format = _ConvertFormat((*ppTexture2D)->desc.Format);


			if (arraySize > 1)
			{
				if ((*ppTexture2D)->desc.MiscFlags & RESOURCE_MISC_TEXTURECUBE)
				{
					if (arraySize > 6)
					{
						srv_desc.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
					}
					else
					{
						srv_desc.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
					}
				}
				else
				{
					if (multisampled)
					{
						srv_desc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; // MSArray?
					}
					else
					{
						srv_desc.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
					}
				}

				if ((*ppTexture2D)->independentSRVArraySlices)
				{
					if ((*ppTexture2D)->desc.MiscFlags & RESOURCE_MISC_TEXTURECUBE)
					{
						UINT slices = arraySize / 6;

						// independent cubemaps
						for (UINT i = 0; i < slices; ++i)
						{
							srv_desc.subresourceRange.baseArrayLayer = i * 6;
							srv_desc.subresourceRange.layerCount = 6;

							(*ppTexture2D)->additionalSRVs_Vulkan.push_back(nullptr);
							res = vkCreateImageView(device, &srv_desc, nullptr, reinterpret_cast<VkImageView*>(&(*ppTexture2D)->additionalSRVs_Vulkan.back()));
							assert(res == VK_SUCCESS);
						}
					}
					else
					{
						UINT slices = arraySize;

						// independent slices
						for (UINT i = 0; i < slices; ++i)
						{
							srv_desc.subresourceRange.baseArrayLayer = i;
							srv_desc.subresourceRange.layerCount = 1;

							(*ppTexture2D)->additionalSRVs_Vulkan.push_back(nullptr);
							res = vkCreateImageView(device, &srv_desc, nullptr, reinterpret_cast<VkImageView*>(&(*ppTexture2D)->additionalSRVs_Vulkan.back()));
							assert(res == VK_SUCCESS);
						}
					}
				}
			}
			else
			{
				if (multisampled)
				{
					srv_desc.viewType = VK_IMAGE_VIEW_TYPE_2D; // MSAA?
				}
				else
				{
					srv_desc.viewType = VK_IMAGE_VIEW_TYPE_2D;

					if ((*ppTexture2D)->independentSRVMIPs)
					{
						// Create subresource SRVs:
						UINT miplevels = (*ppTexture2D)->desc.MipLevels;
						for (UINT i = 0; i < miplevels; ++i)
						{
							srv_desc.subresourceRange.baseMipLevel = i;
							srv_desc.subresourceRange.levelCount = 1;

							(*ppTexture2D)->additionalSRVs_Vulkan.push_back(nullptr);
							res = vkCreateImageView(device, &srv_desc, nullptr, reinterpret_cast<VkImageView*>(&(*ppTexture2D)->additionalSRVs_Vulkan.back()));
							assert(res == VK_SUCCESS);
						}
					}
				}
			}

			// Create full-resource SRV:
			srv_desc.subresourceRange.baseArrayLayer = 0;
			srv_desc.subresourceRange.layerCount = (*ppTexture2D)->desc.ArraySize;
			srv_desc.subresourceRange.baseMipLevel = 0;
			srv_desc.subresourceRange.levelCount = (*ppTexture2D)->desc.MipLevels;

			res = vkCreateImageView(device, &srv_desc, nullptr, reinterpret_cast<VkImageView*>(&(*ppTexture2D)->SRV_Vulkan));
			hr = res == VK_SUCCESS;
			assert(SUCCEEDED(hr));
		}

		if ((*ppTexture2D)->desc.BindFlags & BIND_UNORDERED_ACCESS)
		{
		}


		return hr;
	}
	HRESULT GraphicsDevice_Vulkan::CreateTexture3D(const Texture3DDesc* pDesc, const SubresourceData *pInitialData, Texture3D **ppTexture3D)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateInputLayout(const VertexLayoutDesc *pInputElementDescs, UINT NumElements,
		const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength, VertexLayout *pInputLayout)
	{
		pInputLayout->desc.reserve((size_t)NumElements);
		for (UINT i = 0; i < NumElements; ++i)
		{
			pInputLayout->desc.push_back(pInputElementDescs[i]);
		}

		return S_OK;
	}
	HRESULT GraphicsDevice_Vulkan::CreateVertexShader(const void *pShaderBytecode, SIZE_T BytecodeLength, VertexShader *pVertexShader)
	{
		pVertexShader->code.data = new BYTE[BytecodeLength];
		memcpy(pVertexShader->code.data, pShaderBytecode, BytecodeLength);
		pVertexShader->code.size = BytecodeLength;

		return (pVertexShader->code.data != nullptr && pVertexShader->code.size > 0 ? S_OK : E_FAIL);
	}
	HRESULT GraphicsDevice_Vulkan::CreatePixelShader(const void *pShaderBytecode, SIZE_T BytecodeLength, PixelShader *pPixelShader)
	{
		pPixelShader->code.data = new BYTE[BytecodeLength];
		memcpy(pPixelShader->code.data, pShaderBytecode, BytecodeLength);
		pPixelShader->code.size = BytecodeLength;

		return (pPixelShader->code.data != nullptr && pPixelShader->code.size > 0 ? S_OK : E_FAIL);
	}
	HRESULT GraphicsDevice_Vulkan::CreateGeometryShader(const void *pShaderBytecode, SIZE_T BytecodeLength, GeometryShader *pGeometryShader)
	{
		pGeometryShader->code.data = new BYTE[BytecodeLength];
		memcpy(pGeometryShader->code.data, pShaderBytecode, BytecodeLength);
		pGeometryShader->code.size = BytecodeLength;

		return (pGeometryShader->code.data != nullptr && pGeometryShader->code.size > 0 ? S_OK : E_FAIL);
	}
	HRESULT GraphicsDevice_Vulkan::CreateHullShader(const void *pShaderBytecode, SIZE_T BytecodeLength, HullShader *pHullShader)
	{
		pHullShader->code.data = new BYTE[BytecodeLength];
		memcpy(pHullShader->code.data, pShaderBytecode, BytecodeLength);
		pHullShader->code.size = BytecodeLength;

		return (pHullShader->code.data != nullptr && pHullShader->code.size > 0 ? S_OK : E_FAIL);
	}
	HRESULT GraphicsDevice_Vulkan::CreateDomainShader(const void *pShaderBytecode, SIZE_T BytecodeLength, DomainShader *pDomainShader)
	{
		pDomainShader->code.data = new BYTE[BytecodeLength];
		memcpy(pDomainShader->code.data, pShaderBytecode, BytecodeLength);
		pDomainShader->code.size = BytecodeLength;

		return (pDomainShader->code.data != nullptr && pDomainShader->code.size > 0 ? S_OK : E_FAIL);
	}
	HRESULT GraphicsDevice_Vulkan::CreateComputeShader(const void *pShaderBytecode, SIZE_T BytecodeLength, ComputeShader *pComputeShader)
	{
		pComputeShader->code.data = new BYTE[BytecodeLength];
		memcpy(pComputeShader->code.data, pShaderBytecode, BytecodeLength);
		pComputeShader->code.size = BytecodeLength;

		return (pComputeShader->code.data != nullptr && pComputeShader->code.size > 0 ? S_OK : E_FAIL);
	}
	HRESULT GraphicsDevice_Vulkan::CreateBlendState(const BlendStateDesc *pBlendStateDesc, BlendState *pBlendState)
	{
		pBlendState->desc = *pBlendStateDesc;
		return S_OK;
	}
	HRESULT GraphicsDevice_Vulkan::CreateDepthStencilState(const DepthStencilStateDesc *pDepthStencilStateDesc, DepthStencilState *pDepthStencilState)
	{
		pDepthStencilState->desc = *pDepthStencilStateDesc;
		return S_OK;
	}
	HRESULT GraphicsDevice_Vulkan::CreateRasterizerState(const RasterizerStateDesc *pRasterizerStateDesc, RasterizerState *pRasterizerState)
	{
		pRasterizerState->desc = *pRasterizerStateDesc;
		return S_OK;
	}
	HRESULT GraphicsDevice_Vulkan::CreateSamplerState(const SamplerDesc *pSamplerDesc, Sampler *pSamplerState)
	{
		pSamplerState->desc = *pSamplerDesc;

		VkSamplerCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

		if (vkCreateSampler(device, &createInfo, nullptr, reinterpret_cast<VkSampler*>(&pSamplerState->resource_Vulkan)) != VK_SUCCESS) {
			throw std::runtime_error("failed to create sampler!");
		}

		return S_OK;
	}
	HRESULT GraphicsDevice_Vulkan::CreateQuery(const GPUQueryDesc *pDesc, GPUQuery *pQuery)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::CreateGraphicsPSO(const GraphicsPSODesc* pDesc, GraphicsPSO* pso)
	{
		pso->desc = *pDesc;

		std::vector<VkAttachmentDescription> attachments;
		std::vector<VkAttachmentReference> colorAttachmentRefs;

		attachments.reserve(pDesc->numRTs);
		colorAttachmentRefs.reserve(pDesc->numRTs);

		for (UINT i = 0; i < pDesc->numRTs; ++i)
		{
			VkAttachmentDescription attachment = {};
			attachment.format = _ConvertFormat(pDesc->RTFormats[i]);
			attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			attachments.push_back(attachment);

			VkAttachmentReference ref = {};
			ref.attachment = i;
			ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			colorAttachmentRefs.push_back(ref);
		}


		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = pDesc->numRTs;
		subpass.pColorAttachments = colorAttachmentRefs.data();

		VkAttachmentDescription depthAttachment = {};
		VkAttachmentReference depthAttachmentRef = {};
		if (pDesc->DSFormat != FORMAT_UNKNOWN)
		{
			depthAttachment.format = _ConvertFormat(pDesc->DSFormat);
			depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // hmmm...
			depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // hmmm...
			depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
			depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			depthAttachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;
			attachments.push_back(depthAttachment);

			depthAttachmentRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
			depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			subpass.pDepthStencilAttachment = &depthAttachmentRef;
		}

		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;

		if (vkCreateRenderPass(device, &renderPassInfo, nullptr, reinterpret_cast<VkRenderPass*>(&pso->renderPass_Vulkan)) != VK_SUCCESS) {
			throw std::runtime_error("failed to create render pass!");
		}



		VkGraphicsPipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = defaultPipelineLayout_Graphics;
		pipelineInfo.renderPass = static_cast<VkRenderPass>(pso->renderPass_Vulkan);
		pipelineInfo.subpass = 0;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;


		// Shaders:

		std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

		if (pDesc->vs != nullptr)
		{
			VkShaderModuleCreateInfo moduleInfo = {}; 
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = pDesc->vs->code.size;
			moduleInfo.pCode = reinterpret_cast<const uint32_t*>(pDesc->vs->code.data);
			VkShaderModule shaderModule;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			VkPipelineShaderStageCreateInfo stageInfo = {}; 
			stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
			stageInfo.module = shaderModule;
			stageInfo.pName = "main";

			shaderStages.push_back(stageInfo);
		}

		if (pDesc->hs != nullptr)
		{
			VkShaderModuleCreateInfo moduleInfo = {};
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = pDesc->hs->code.size;
			moduleInfo.pCode = reinterpret_cast<const uint32_t*>(pDesc->hs->code.data);
			VkShaderModule shaderModule;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			VkPipelineShaderStageCreateInfo stageInfo = {};
			stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.stage = VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
			stageInfo.module = shaderModule;
			stageInfo.pName = "main";

			shaderStages.push_back(stageInfo);
		}

		if (pDesc->ds != nullptr)
		{
			VkShaderModuleCreateInfo moduleInfo = {};
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = pDesc->ds->code.size;
			moduleInfo.pCode = reinterpret_cast<const uint32_t*>(pDesc->ds->code.data);
			VkShaderModule shaderModule;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			VkPipelineShaderStageCreateInfo stageInfo = {};
			stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.stage = VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
			stageInfo.module = shaderModule;
			stageInfo.pName = "main";

			shaderStages.push_back(stageInfo);
		}

		if (pDesc->gs != nullptr)
		{
			VkShaderModuleCreateInfo moduleInfo = {};
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = pDesc->gs->code.size;
			moduleInfo.pCode = reinterpret_cast<const uint32_t*>(pDesc->gs->code.data);
			VkShaderModule shaderModule;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			VkPipelineShaderStageCreateInfo stageInfo = {};
			stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.stage = VK_SHADER_STAGE_GEOMETRY_BIT;
			stageInfo.module = shaderModule;
			stageInfo.pName = "main";

			shaderStages.push_back(stageInfo);
		}

		if (pDesc->ps != nullptr)
		{
			VkShaderModuleCreateInfo moduleInfo = {};
			moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			moduleInfo.codeSize = pDesc->ps->code.size;
			moduleInfo.pCode = reinterpret_cast<const uint32_t*>(pDesc->ps->code.data);
			VkShaderModule shaderModule;
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			VkPipelineShaderStageCreateInfo stageInfo = {};
			stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
			stageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			stageInfo.module = shaderModule;
			stageInfo.pName = "main";

			shaderStages.push_back(stageInfo);
		}

		pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineInfo.pStages = shaderStages.data();


		// Fixed function states:

		// Input layout:
		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
		vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		std::vector<VkVertexInputBindingDescription> bindings;
		std::vector<VkVertexInputAttributeDescription> attributes;
		if (pDesc->il != nullptr)
		{
			uint32_t lastBinding = 0xFFFFFFFF;
			for (auto& x : pDesc->il->desc)
			{
				VkVertexInputBindingDescription bind = {};
				bind.binding = x.InputSlot;
				bind.inputRate = x.InputSlotClass == INPUT_PER_VERTEX_DATA ? VK_VERTEX_INPUT_RATE_VERTEX : VK_VERTEX_INPUT_RATE_INSTANCE;
				bind.stride = x.AlignedByteOffset;
				if (bind.stride == APPEND_ALIGNED_ELEMENT)
				{
					// need to manually resolve this from the format spec.
					bind.stride = GetFormatStride(x.Format);
				}

				if (lastBinding != bind.binding)
				{
					bindings.push_back(bind);
					lastBinding = bind.binding;
				}
				else
				{
					bindings.back().stride += bind.stride;
				}
			}

			uint32_t i = 0;
			for (auto& x : pDesc->il->desc)
			{
				VkVertexInputAttributeDescription attr = {};
				attr.binding = x.InputSlot;
				attr.format = _ConvertFormat(x.Format);
				attr.location = i; 
				attr.offset = x.AlignedByteOffset;
				if (attr.offset == APPEND_ALIGNED_ELEMENT)
				{
					// need to manually resolve this from the format spec.
					attr.offset = GetFormatStride(x.Format);
				}

				attributes.push_back(attr);

				i++;
			}

			vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
			vertexInputInfo.pVertexBindingDescriptions = bindings.data();
			vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
			vertexInputInfo.pVertexAttributeDescriptions = attributes.data();
		}
		pipelineInfo.pVertexInputState = &vertexInputInfo;

		// Primitive type:
		//		TODO: This doesn't match DX12!
		VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
		inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		switch (pDesc->ptt)
		{
		case PRIMITIVE_TOPOLOGY_TYPE_POINT:
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
			break;
		case PRIMITIVE_TOPOLOGY_TYPE_LINE:
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
			break;
		case PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			break;
		case PRIMITIVE_TOPOLOGY_TYPE_PATCH:
			inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
			break;
		default:
			break;
		}
		inputAssembly.primitiveRestartEnable = VK_FALSE;

		pipelineInfo.pInputAssemblyState = &inputAssembly;


		// Rasterizer:
		VkPipelineRasterizationStateCreateInfo rasterizer = {};
		rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable = VK_FALSE;
		rasterizer.rasterizerDiscardEnable = VK_FALSE;
		rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth = 1.0f;
		rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
		rasterizer.depthBiasEnable = VK_FALSE;
		rasterizer.depthBiasConstantFactor = 0.0f; // Optional
		rasterizer.depthBiasClamp = 0.0f; // Optional
		rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

		pipelineInfo.pRasterizationState = &rasterizer;


		// Viewport, Scissor:
		VkViewport viewport = {};
		viewport.x = 0;
		viewport.y = 0;
		viewport.width = 65535;
		viewport.height = 65535;
		viewport.minDepth = 0;
		viewport.maxDepth = 1;

		VkRect2D scissor = {};
		scissor.extent.width = 65535;
		scissor.extent.height = 65535;

		VkPipelineViewportStateCreateInfo viewportState = {};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.pViewports = &viewport;
		viewportState.scissorCount = 1;
		viewportState.pScissors = &scissor;

		pipelineInfo.pViewportState = &viewportState;


		// Depth-Stencil:
		VkPipelineDepthStencilStateCreateInfo depthstencil = {};
		depthstencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		if (pDesc->dss != nullptr)
		{
			depthstencil.depthTestEnable = pDesc->dss->desc.DepthEnable ? 1 : 0;
			depthstencil.depthWriteEnable = pDesc->dss->desc.DepthWriteMask != DEPTH_WRITE_MASK_ZERO;

			depthstencil.stencilTestEnable = pDesc->dss->desc.StencilEnable ? 1 : 0;
		}

		pipelineInfo.pDepthStencilState = &depthstencil;


		// MSAA:
		VkPipelineMultisampleStateCreateInfo multisampling = {};
		multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable = VK_FALSE;
		multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampling.minSampleShading = 1.0f; // Optional
		multisampling.pSampleMask = nullptr; // Optional
		multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
		multisampling.alphaToOneEnable = VK_FALSE; // Optional

		pipelineInfo.pMultisampleState = &multisampling;


		// Blending:
		std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(subpass.colorAttachmentCount);
		for (size_t i = 0; i < colorBlendAttachments.size(); ++i)
		{
			colorBlendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			colorBlendAttachments[i].blendEnable = VK_FALSE;
			colorBlendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
			colorBlendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
			colorBlendAttachments[i].colorBlendOp = VK_BLEND_OP_ADD; // Optional
			colorBlendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
			colorBlendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
			colorBlendAttachments[i].alphaBlendOp = VK_BLEND_OP_ADD; // Optional
		}

		VkPipelineColorBlendStateCreateInfo colorBlending = {};
		colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlending.logicOpEnable = VK_FALSE;
		colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
		colorBlending.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
		colorBlending.pAttachments = colorBlendAttachments.data();
		colorBlending.blendConstants[0] = 0.0f; // Optional
		colorBlending.blendConstants[1] = 0.0f; // Optional
		colorBlending.blendConstants[2] = 0.0f; // Optional
		colorBlending.blendConstants[3] = 0.0f; // Optional

		pipelineInfo.pColorBlendState = &colorBlending;


		// Tessellation:
		VkPipelineTessellationStateCreateInfo tessellationInfo = {};
		tessellationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
		tessellationInfo.patchControlPoints = 3;

		pipelineInfo.pTessellationState = &tessellationInfo;




		// Dynamic state will be specified at runtime:
		VkDynamicState dynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_LINE_WIDTH,
			VK_DYNAMIC_STATE_STENCIL_REFERENCE
		};

		VkPipelineDynamicStateCreateInfo dynamicState = {};
		dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicState.dynamicStateCount = ARRAYSIZE(dynamicStates);
		dynamicState.pDynamicStates = dynamicStates;

		pipelineInfo.pDynamicState = &dynamicState;


		VkResult res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, reinterpret_cast<VkPipeline*>(&pso->pipeline_Vulkan));
		HRESULT hr = res == VK_SUCCESS ? S_OK : E_FAIL;
		//assert(SUCCEEDED(hr));

		return hr;
	}
	HRESULT GraphicsDevice_Vulkan::CreateComputePSO(const ComputePSODesc* pDesc, ComputePSO* pso)
	{
		pso->desc = *pDesc;

		VkComputePipelineCreateInfo pipelineInfo = {};
		pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
		pipelineInfo.layout = defaultPipelineLayout_Compute;
		pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;



		VkShaderModuleCreateInfo moduleInfo = {};
		moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;

		VkPipelineShaderStageCreateInfo stageInfo = {};
		stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;

		VkShaderModule shaderModule = {};

		if (pDesc->cs != nullptr)
		{
			moduleInfo.codeSize = pDesc->cs->code.size;
			moduleInfo.pCode = reinterpret_cast<const uint32_t*>(pDesc->cs->code.data);
			if (vkCreateShaderModule(device, &moduleInfo, nullptr, &shaderModule) != VK_SUCCESS) {
				throw std::runtime_error("failed to create shader module!");
			}

			stageInfo.module = shaderModule;
			stageInfo.pName = "main";

			pipelineInfo.stage = stageInfo;
		}


		VkResult res = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, reinterpret_cast<VkPipeline*>(&pso->pipeline_Vulkan));
		HRESULT hr = res == VK_SUCCESS ? S_OK : E_FAIL;
		//assert(SUCCEEDED(hr));

		return hr;
	}


	void GraphicsDevice_Vulkan::PresentBegin()
	{
		LOCK();


		// Sync up copy queue:
		copyQueueLock.lock();
		{
			if (vkEndCommandBuffer(copyCommandBuffer) != VK_SUCCESS) {
				throw std::runtime_error("failed to record copy command buffer!");
			}

			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			if (vkQueueSubmit(copyQueue, 1, &submitInfo, copyFence) != VK_SUCCESS) {
				throw std::runtime_error("failed to submit copy command buffer!");
			}

			VkResult res;

			//vkQueueWaitIdle(copyQueue);

			res = vkWaitForFences(device, 1, &copyFence, true, 0xFFFFFFFFFFFFFFFF);
			assert(res == VK_SUCCESS);

			res = vkResetFences(device, 1, &copyFence);
			assert(res == VK_SUCCESS);


			res = vkResetCommandPool(device, copyCommandPool, 0);
			assert(res == VK_SUCCESS);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			beginInfo.pInheritanceInfo = nullptr; // Optional

			res = vkBeginCommandBuffer(copyCommandBuffer, &beginInfo);
			assert(res == VK_SUCCESS);
		}
		copyQueueLock.unlock();



		VkClearValue clearColor = { (FRAMECOUNT % 256) / 255.0f, 0.0f, 0.0f, 1.0f };

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = defaultRenderPass;
		renderPassInfo.framebuffer = GetFrameResources().swapChainFramebuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;

		// Begin presentation render pass...
		vkCmdBeginRenderPass(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);


		VkClearAttachment clearInfo = {};
		clearInfo.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		clearInfo.clearValue = clearColor;
		clearInfo.colorAttachment = 0;

		VkClearRect rect = {};
		rect.baseArrayLayer = 0;
		rect.layerCount = 1;
		rect.rect.offset.x = 0;
		rect.rect.offset.y = 0;
		rect.rect.extent.width = SCREENWIDTH;
		rect.rect.extent.height = SCREENHEIGHT;

		vkCmdClearAttachments(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE), 1, &clearInfo, 1, &rect);


	}
	void GraphicsDevice_Vulkan::PresentEnd()
	{
		VkResult res;

		uint64_t currentframe = GetFrameCount() % BACKBUFFER_COUNT;

		// ...end presentation render pass
		vkCmdEndRenderPass(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE));

		if (vkEndCommandBuffer(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE)) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}


		uint32_t imageIndex;
		vkAcquireNextImageKHR(device, swapChain, 0xFFFFFFFFFFFFFFFF, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
		assert(imageIndex == currentframe);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = GetFrameResources().commandBuffers;

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, GetFrameResources().frameFence) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}


		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { swapChain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr; // Optional

		vkQueuePresentKHR(presentQueue, &presentInfo);

		//vkQueueWaitIdle(presentQueue);


		// This acts as a barrier, following this we will be using the next frame's resources when calling GetFrameResources()!
		FRAMECOUNT++;


		// Initiate stalling CPU when GPU is behind by more frames than would fit in the backbuffers:
		if (FRAMECOUNT >= BACKBUFFER_COUNT && vkGetFenceStatus(device, GetFrameResources().frameFence) == VK_SUCCESS)
		{
			res = vkWaitForFences(device, 1, &GetFrameResources().frameFence, true, 0xFFFFFFFFFFFFFFFF);
			assert(res == VK_SUCCESS);

			res = vkResetFences(device, 1, &GetFrameResources().frameFence);
			assert(res == VK_SUCCESS);
		}
		
		for (int threadID = 0; threadID < GRAPHICSTHREAD_IMMEDIATE + 1; ++threadID) // todo: all command lists
		{
			res = vkResetCommandPool(device, GetFrameResources().commandPools[threadID], 0);
			assert(res == VK_SUCCESS);

			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
			beginInfo.pInheritanceInfo = nullptr; // Optional

			res = vkBeginCommandBuffer(GetFrameResources().commandBuffers[threadID], &beginInfo);
			assert(res == VK_SUCCESS);


			// reset descriptor allocators:
			GetFrameResources().ResourceDescriptorsGPU[threadID]->reset();

			// reset immediate resource allocators:
			GetFrameResources().resourceBuffer[threadID]->clear();

			memset(renderPassActive, 0, sizeof(renderPassActive));
			memset(attachments, 0, sizeof(attachments));
			attachmentCount = 0;
		}

		RESOLUTIONCHANGED = false;

		UNLOCK();
	}

	void GraphicsDevice_Vulkan::ExecuteDeferredContexts()
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = GRAPHICSTHREAD_COUNT - 1;
		submitInfo.pCommandBuffers = &GetFrameResources().commandBuffers[GRAPHICSTHREAD_IMMEDIATE + 1];

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}
	}
	void GraphicsDevice_Vulkan::FinishCommandList(GRAPHICSTHREAD threadID)
	{
		if (threadID == GRAPHICSTHREAD_IMMEDIATE)
			return;
		if (vkEndCommandBuffer(GetDirectCommandList(threadID)) != VK_SUCCESS) {
			throw std::runtime_error("failed to record command buffer!");
		}
	}


	void GraphicsDevice_Vulkan::BindViewports(UINT NumViewports, const ViewPort *pViewports, GRAPHICSTHREAD threadID)
	{
		assert(NumViewports <= 6);
		VkViewport viewports[6];
		for (UINT i = 0; i < NumViewports; ++i)
		{
			viewports[i].x = pViewports[i].TopLeftX;
			viewports[i].y = pViewports[i].TopLeftY;
			viewports[i].width = pViewports[i].Width;
			viewports[i].height = pViewports[i].Height;
			viewports[i].minDepth = pViewports[i].MinDepth;
			viewports[i].maxDepth = pViewports[i].MaxDepth;
		}
		vkCmdSetViewport(GetDirectCommandList(threadID), 0, NumViewports, viewports);
	}
	void GraphicsDevice_Vulkan::BindRenderTargetsUAVs(UINT NumViews, Texture* const *ppRenderTargets, Texture2D* depthStencilTexture, GPUResource* const *ppUAVs, int slotUAV, int countUAV,
		GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindRenderTargets(UINT NumViews, Texture* const *ppRenderTargets, Texture2D* depthStencilTexture, GRAPHICSTHREAD threadID, int arrayIndex)
	{
		assert(NumViews <= 8);
		for (UINT i = 0; i < NumViews; ++i)
		{
			attachments[threadID][i] = static_cast<VkImageView>(ppRenderTargets[i]->SRV_Vulkan); // SRV -> RTV
		}
		attachmentCount = NumViews;

		if (depthStencilTexture != nullptr)
		{
			attachments[threadID][attachmentCount] = static_cast<VkImageView>(depthStencilTexture->SRV_Vulkan); // SRV -> DSV
			attachmentCount++;
		}

	}
	void GraphicsDevice_Vulkan::ClearRenderTarget(Texture* pTexture, const FLOAT ColorRGBA[4], GRAPHICSTHREAD threadID, int arrayIndex)
	{
		//VkClearValue clearColor = { ColorRGBA[0], ColorRGBA[1], ColorRGBA[2], ColorRGBA[3] };

		//VkClearAttachment clearInfo = {};
		//clearInfo.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		//clearInfo.clearValue = clearColor;
		//clearInfo.colorAttachment = 0;

		//VkClearRect rect = {};
		//rect.baseArrayLayer = 0;
		//rect.layerCount = 1;
		//rect.rect.offset.x = 0;
		//rect.rect.offset.y = 0;
		//rect.rect.extent.width = SCREENWIDTH;
		//rect.rect.extent.height = SCREENHEIGHT;

		//vkCmdClearAttachments(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE), 1, &clearInfo, 1, &rect);
	}
	void GraphicsDevice_Vulkan::ClearDepthStencil(Texture2D* pTexture, UINT ClearFlags, FLOAT Depth, UINT8 Stencil, GRAPHICSTHREAD threadID, int arrayIndex)
	{
		//VkClearValue clearColor;
		//clearColor.depthStencil.depth = Depth;
		//clearColor.depthStencil.stencil = Stencil;

		//VkClearAttachment clearInfo = {};
		//clearInfo.aspectMask = 0;
		//clearInfo.aspectMask |= ClearFlags & CLEAR_DEPTH ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
		//clearInfo.aspectMask |= ClearFlags & CLEAR_STENCIL ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
		//clearInfo.clearValue = clearColor;
		//clearInfo.colorAttachment = 0;

		//VkClearRect rect = {};
		//rect.baseArrayLayer = 0;
		//rect.layerCount = 1;
		//rect.rect.offset.x = 0;
		//rect.rect.offset.y = 0;
		//rect.rect.extent.width = SCREENWIDTH;
		//rect.rect.extent.height = SCREENHEIGHT;

		//vkCmdClearAttachments(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE), 1, &clearInfo, 1, &rect);
	}
	void GraphicsDevice_Vulkan::BindResource(SHADERSTAGE stage, GPUResource* resource, int slot, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindResources(SHADERSTAGE stage, GPUResource *const* resources, int slot, int count, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResource(SHADERSTAGE stage, GPUResource* resource, int slot, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResources(SHADERSTAGE stage, GPUResource *const* resources, int slot, int count, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResourceCS(GPUResource* resource, int slot, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::BindUnorderedAccessResourcesCS(GPUResource *const* resources, int slot, int count, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::UnBindResources(int slot, int num, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::UnBindUnorderedAccessResources(int slot, int num, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindSampler(SHADERSTAGE stage, Sampler* sampler, int slot, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindConstantBuffer(SHADERSTAGE stage, GPUBuffer* buffer, int slot, GRAPHICSTHREAD threadID)
	{
		if (buffer != nullptr && buffer->resource_Vulkan != nullptr)
		{
			VkDescriptorBufferInfo bufferInfo = {};
			bufferInfo.buffer = static_cast<VkBuffer>(buffer->resource_Vulkan);
			bufferInfo.offset = 0;
			bufferInfo.range = buffer->desc.ByteWidth;

			VkWriteDescriptorSet descriptorWrite = {};
			descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrite.dstSet = GetFrameResources().ResourceDescriptorsGPU[threadID]->descriptorSet_CPU[stage];
			descriptorWrite.dstBinding = VULKAN_DESCRIPTOR_SET_OFFSET_CBV;
			descriptorWrite.dstArrayElement = slot;
			descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrite.descriptorCount = 1;
			descriptorWrite.pBufferInfo = &bufferInfo;
			descriptorWrite.pImageInfo = nullptr; // Optional
			descriptorWrite.pTexelBufferView = nullptr; // Optional

			vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
			GetFrameResources().ResourceDescriptorsGPU[threadID]->dirty[stage] = true;
		}
	}
	void GraphicsDevice_Vulkan::BindVertexBuffers(GPUBuffer* const *vertexBuffers, int slot, int count, const UINT* strides, const UINT* offsets, GRAPHICSTHREAD threadID)
	{
		//VkDeviceSize voffsets[8] = {};
		//VkBuffer vbuffers[8] = {};
		//assert(count <= 8);
		//for (int i = 0; i < count; ++i)
		//{
		//	if (vertexBuffers[i] != nullptr)
		//	{
		//		vbuffers[i] = static_cast<VkBuffer>(vertexBuffers[i]->resource_Vulkan);
		//	}
		//	if (offsets != nullptr)
		//	{
		//		voffsets[i] = offsets[i];
		//	}
		//}
		//vkCmdBindVertexBuffers(GetDirectCommandList(threadID), static_cast<uint32_t>(slot), static_cast<uint32_t>(count), vbuffers, voffsets);
	}
	void GraphicsDevice_Vulkan::BindIndexBuffer(GPUBuffer* indexBuffer, const INDEXBUFFER_FORMAT format, UINT offset, GRAPHICSTHREAD threadID)
	{
		//if (indexBuffer != nullptr)
		//{
		//	vkCmdBindIndexBuffer(GetDirectCommandList(threadID), static_cast<VkBuffer>(indexBuffer->resource_Vulkan), offset, format == INDEXFORMAT_16BIT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
		//}
	}
	void GraphicsDevice_Vulkan::BindPrimitiveTopology(PRIMITIVETOPOLOGY type, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindStencilRef(UINT value, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindBlendFactor(XMFLOAT4 value, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::BindGraphicsPSO(GraphicsPSO* pso, GRAPHICSTHREAD threadID)
	{
		vkCmdBindPipeline(GetDirectCommandList(threadID), VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<VkPipeline>(pso->pipeline_Vulkan));

		VkRenderPass renderPass = static_cast<VkRenderPass>(pso->renderPass_Vulkan);

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = attachmentCount;
		framebufferInfo.pAttachments = attachments[threadID];
		framebufferInfo.width = swapChainExtent.width;
		framebufferInfo.height = swapChainExtent.height;
		framebufferInfo.layers = 1;

		VkFramebuffer frameBuffer;
		if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frameBuffer) != VK_SUCCESS) {
			throw std::runtime_error("failed to create framebuffer!");
		}


		static const VkClearValue clearColor = { 0.0f, 0.0f, 0.0f, 1.0f };

		VkRenderPassBeginInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassInfo.renderPass = renderPass;
		renderPassInfo.framebuffer = frameBuffer;
		renderPassInfo.renderArea.offset = { 0, 0 };
		renderPassInfo.renderArea.extent = swapChainExtent;
		renderPassInfo.clearValueCount = 1;
		renderPassInfo.pClearValues = &clearColor;


		if (renderPassActive[threadID])
		{
			vkCmdEndRenderPass(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE));
		}
		vkCmdBeginRenderPass(GetDirectCommandList(GRAPHICSTHREAD_IMMEDIATE), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		renderPassActive[threadID] = true;
	}
	void GraphicsDevice_Vulkan::BindComputePSO(ComputePSO* pso, GRAPHICSTHREAD threadID)
	{
		vkCmdBindPipeline(GetDirectCommandList(threadID), VK_PIPELINE_BIND_POINT_COMPUTE, static_cast<VkPipeline>(pso->pipeline_Vulkan));
	}
	void GraphicsDevice_Vulkan::Draw(int vertexCount, UINT startVertexLocation, GRAPHICSTHREAD threadID)
	{
		GetFrameResources().ResourceDescriptorsGPU[threadID]->validate(GetDirectCommandList(threadID));
		vkCmdDraw(GetDirectCommandList(threadID), static_cast<uint32_t>(vertexCount), 1, startVertexLocation, 0);
	}
	void GraphicsDevice_Vulkan::DrawIndexed(int indexCount, UINT startIndexLocation, UINT baseVertexLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawInstanced(int vertexCount, int instanceCount, UINT startVertexLocation, UINT startInstanceLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawIndexedInstanced(int indexCount, int instanceCount, UINT startIndexLocation, UINT baseVertexLocation, UINT startInstanceLocation, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawInstancedIndirect(GPUBuffer* args, UINT args_offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::DrawIndexedInstancedIndirect(GPUBuffer* args, UINT args_offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::Dispatch(UINT threadGroupCountX, UINT threadGroupCountY, UINT threadGroupCountZ, GRAPHICSTHREAD threadID)
	{
		GetFrameResources().ResourceDescriptorsGPU[threadID]->validate(GetDirectCommandList(threadID));
		vkCmdDispatch(GetDirectCommandList(threadID), threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}
	void GraphicsDevice_Vulkan::DispatchIndirect(GPUBuffer* args, UINT args_offset, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::GenerateMips(Texture* texture, GRAPHICSTHREAD threadID, int arrayIndex)
	{
	}
	void GraphicsDevice_Vulkan::CopyTexture2D(Texture2D* pDst, Texture2D* pSrc, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::CopyTexture2D_Region(Texture2D* pDst, UINT dstMip, UINT dstX, UINT dstY, Texture2D* pSrc, UINT srcMip, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::MSAAResolve(Texture2D* pDst, Texture2D* pSrc, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::UpdateBuffer(GPUBuffer* buffer, const void* data, GRAPHICSTHREAD threadID, int dataSize)
	{
		assert(buffer->desc.Usage != USAGE_IMMUTABLE && "Cannot update IMMUTABLE GPUBuffer!");
		assert((int)buffer->desc.ByteWidth >= dataSize || dataSize < 0 && "Data size is too big!");

		if (dataSize == 0)
		{
			return;
		}

		dataSize = min((int)buffer->desc.ByteWidth, dataSize);
		dataSize = (dataSize >= 0 ? dataSize : buffer->desc.ByteWidth);

		// issue data copy:
		uint8_t* dest = GetFrameResources().resourceBuffer[threadID]->allocate(dataSize, 4);
		memcpy(dest, data, dataSize);

		VkBufferCopy copyRegion = {};
		copyRegion.size = dataSize;
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = GetFrameResources().resourceBuffer[threadID]->calculateOffset(dest);

		copyQueueLock.lock();
		vkCmdCopyBuffer(copyCommandBuffer, GetFrameResources().resourceBuffer[threadID]->resource, static_cast<VkBuffer>(buffer->resource_Vulkan), 1, &copyRegion);
		copyQueueLock.unlock();

	}
	void* GraphicsDevice_Vulkan::AllocateFromRingBuffer(GPURingBuffer* buffer, size_t dataSize, UINT& offsetIntoBuffer, GRAPHICSTHREAD threadID)
	{
		assert(buffer->desc.Usage == USAGE_DYNAMIC && (buffer->desc.CPUAccessFlags & CPU_ACCESS_WRITE) && "Ringbuffer must be writable by the CPU!");
		assert(buffer->desc.ByteWidth > dataSize && "Data of the required size cannot fit!");

		if (dataSize == 0)
		{
			return nullptr;
		}

		dataSize = min(buffer->desc.ByteWidth, dataSize);

		size_t position = buffer->byteOffset;
		bool wrap = position + dataSize > buffer->desc.ByteWidth || buffer->residentFrame != FRAMECOUNT;
		position = wrap ? 0 : position;

		// TODO: realloc on wrap or something


		// provide immediate buffer allocation address and issue deferred data copy:
		uint8_t* dest = GetFrameResources().resourceBuffer[threadID]->allocate(dataSize, 4);

		VkBufferCopy copyRegion = {};
		copyRegion.size = dataSize;
		copyRegion.srcOffset = 0;
		copyRegion.dstOffset = GetFrameResources().resourceBuffer[threadID]->calculateOffset(dest);

		copyQueueLock.lock();
		vkCmdCopyBuffer(copyCommandBuffer, GetFrameResources().resourceBuffer[threadID]->resource, static_cast<VkBuffer>(buffer->resource_Vulkan), 1, &copyRegion);
		copyQueueLock.unlock();



		// Thread safety is compromised!
		buffer->byteOffset = position + dataSize;
		buffer->residentFrame = FRAMECOUNT;

		offsetIntoBuffer = (UINT)position;
		return reinterpret_cast<void*>(dest);
	}
	void GraphicsDevice_Vulkan::InvalidateBufferAccess(GPUBuffer* buffer, GRAPHICSTHREAD threadID)
	{
	}
	bool GraphicsDevice_Vulkan::DownloadBuffer(GPUBuffer* bufferToDownload, GPUBuffer* bufferDest, void* dataDest, GRAPHICSTHREAD threadID)
	{
		return false;
	}
	void GraphicsDevice_Vulkan::SetScissorRects(UINT numRects, const Rect* rects, GRAPHICSTHREAD threadID)
	{
		assert(rects != nullptr);
		assert(numRects <= 8);
		VkRect2D scissors[8];
		for (UINT i = 0; i < numRects; ++i)
		{
			scissors[i].extent.width = abs(rects[i].right - rects[i].left);
			scissors[i].extent.height = abs(rects[i].bottom - rects[i].top);
			scissors[i].offset.x = rects[i].left;
			scissors[i].offset.y = rects[i].bottom;
		}
		vkCmdSetScissor(GetDirectCommandList(threadID), 0, numRects, scissors);
	}

	void GraphicsDevice_Vulkan::WaitForGPU()
	{
	}

	void GraphicsDevice_Vulkan::QueryBegin(GPUQuery *query, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::QueryEnd(GPUQuery *query, GRAPHICSTHREAD threadID)
	{
	}
	bool GraphicsDevice_Vulkan::QueryRead(GPUQuery *query, GRAPHICSTHREAD threadID)
	{
		return true;
	}

	void GraphicsDevice_Vulkan::UAVBarrier(GPUResource *const* uavs, UINT NumBarriers, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::TransitionBarrier(GPUResource *const* resources, UINT NumBarriers, RESOURCE_STATES stateBefore, RESOURCE_STATES stateAfter, GRAPHICSTHREAD threadID)
	{
	}


	HRESULT GraphicsDevice_Vulkan::CreateTextureFromFile(const std::string& fileName, Texture2D **ppTexture, bool mipMaps, GRAPHICSTHREAD threadID)
	{
		HRESULT hr = E_FAIL;
		(*ppTexture) = new Texture2D();

		return hr;
	}
	HRESULT GraphicsDevice_Vulkan::SaveTexturePNG(const std::string& fileName, Texture2D *pTexture, GRAPHICSTHREAD threadID)
	{
		return E_FAIL;
	}
	HRESULT GraphicsDevice_Vulkan::SaveTextureDDS(const std::string& fileName, Texture *pTexture, GRAPHICSTHREAD threadID)
	{
		return E_FAIL;
	}

	void GraphicsDevice_Vulkan::EventBegin(const std::string& name, GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::EventEnd(GRAPHICSTHREAD threadID)
	{
	}
	void GraphicsDevice_Vulkan::SetMarker(const std::string& name, GRAPHICSTHREAD threadID)
	{
	}

}


#endif // WICKEDENGINE_BUILD_VULKAN