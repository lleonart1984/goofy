#ifndef GOOFY_STATES_H
#define GOOFY_STATES_H

#pragma region Includes

#define VK_USE_PLATFORM_WIN32_KHR
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#pragma endregion

#include "goofy.internal.h"

namespace goofy {
	namespace states {

#pragma region Structs to Vulkan Enums Conversions

		VkImageUsageFlagBits __Convert(const ImageUsage& usage);

#pragma endregion

		enum class WorkPieceState {
			DISPATCHED,
			POPULATION_COMPLETED,
			SUBMITTED
		};

		struct WorkPiece {
			std::shared_ptr<Process> GraphicProcess = nullptr;
			DispatchMode Dispatch = DispatchMode::MAIN_THREAD;
			int EngineIndex = -1;
			int ManagerIndex = -1;
			WorkPieceState State = WorkPieceState::DISPATCHED;
			std::mutex mutex;
			OneTimeSemaphore AfterPopulated;

			WorkPiece();

			void PopulationCompleted();

			inline void WaitForPopulation();

			inline bool HasBeenSubmitted();
		};

		enum class CommandListState {
			Initial,
			Recording,
			Executable,
			OnGPU
		};

		struct __GPUTask {
			VkDevice device = nullptr;
			VkSemaphore GPUFinished = nullptr;
			std::vector<std::shared_ptr<__GPUTask>> children;
			bool finished = false;

			__GPUTask();

			~__GPUTask();

			void Wait();

			static std::shared_ptr<__GPUTask> CreateSingle(VkDevice device, bool empty);

			static std::shared_ptr<__GPUTask> Union(int count, std::shared_ptr<__GPUTask>* tasks);

			void FillSemaphores(std::vector<VkSemaphore>& semaphores);
		};

		struct __CPUTask {
			std::shared_ptr<WorkPiece> workPiece;
			void Wait();
		};

		struct __CommandListManager {
			VkCommandBuffer vkCmdList;
			EngineType SupportedEngines;
			CommandListState State;

			std::shared_ptr<WorkPiece> current_work = nullptr;

			void __Open();

			void __Close();

			void __Reset();
		};

		struct __CommandQueueManager {
			VkCommandPool pool;
			VkQueue queue;
			VkDevice device;
			EngineType SupportedEngines;
			std::vector<std::shared_ptr<__CommandListManager>> reusableCmdBuffers;
			std::shared_ptr<__CommandListManager> recordingBuffer;
			std::vector<std::shared_ptr<__CommandListManager>> submittedBuffers;
			std::vector<std::shared_ptr<__GPUTask>> submittedTasks;
			std::vector<VkSemaphore> waitingSemaphores;
			std::vector<uint64_t> waitingValues;
			std::vector<VkPipelineStageFlags> waintingStages;
			bool throwErrorIfAbandonedTasks;
			std::mutex sync_populated;
			std::vector<std::shared_ptr<WorkPiece>> populated = {};

			__CommandQueueManager(VkDevice device, int familyIndex, EngineType supported, VkQueue queue, bool throwErrorIfAbandonedTasks);

			~__CommandQueueManager();

			std::shared_ptr<__CommandListManager> FetchNew();

			/// <summary>
			/// Gets a command list buffer ready to be populated.
			/// </summary>
			/// <returns></returns>
			std::shared_ptr<__CommandListManager> Peek();

			/// <summary>
			/// Wait for all dispatched workPieces to finish population
			/// </summary>
			void WaitForPopulation();

			/// <summary>
			/// Submit current recording command buffer to the gpu.
			/// </summary>
			std::shared_ptr<__GPUTask> SubmitCurrent(int count, std::shared_ptr<__GPUTask>* wait_for);

			/// <summary>
			/// Wait for all submitted tasks. This method should be called before starting a frame using this command pool manager.
			/// </summary>
			void WaitForPendings();

			/// <summary>
			/// Should be called every frame in async threads to reuse finished buffers
			/// </summary>
			void Clean();

			void Populating(std::shared_ptr<WorkPiece> task, std::shared_ptr<__CommandListManager> &cmdList);
		};

		struct __EngineManager {

			std::vector<std::shared_ptr<__CommandQueueManager>> Managers;
			std::vector<bool> marked;
			std::vector<VkQueue> Queues;
			int frames = 0;
			int frame_async_threads = 0;
			int async_threads = 0;
			EngineType supportedEngines = EngineType::NONE;
			VkDevice device = nullptr;

			__EngineManager(); // empty constructor for null initialization

			__EngineManager(VkDevice device, int familyIndex, EngineType supportedEngines, int frames, int frame_async_threads, int async_threads, int queues);
			
			~__EngineManager();

			void Dispatch(std::shared_ptr<WorkPiece> workPiece);

			void Flush(int frame);

			void WaitForCompletition(int frame);
			
			void CleanAsyncManagers();

			void MarkForFlush(int managerIdx);

			void FlushMarked(int waitingFor, std::shared_ptr<__GPUTask>* waitingGPU, std::vector<std::shared_ptr<__GPUTask>> &tasks);
		};

		struct __Window {
			void* pWindow;
			bool IsGLFW;
		};

		struct __ResourceData {
			__Device* device;
			bool IsBuffer;
			// Internal resource
			union {
				VkBuffer Buffer;
				VkImage Image;
			};

			VkDeviceMemory Memory = nullptr;

			VkBuffer UploadingStaging = nullptr;
			VkBuffer DownloadingStaging = nullptr;

			__ResourceData(__Device* device, VkImage image, VkDeviceMemory memory) :device(device), IsBuffer(false), Image(image), Memory(memory) {}
			__ResourceData(__Device* device, VkBuffer buffer, VkDeviceMemory memory) :device(device), IsBuffer(true), Buffer(buffer), Memory(memory) {}
			~__ResourceData();
		};

		struct BufferSliceDescription {
			VkFormat TexelFormat;
			int offset;
			int size;
		};

		struct ImageSliceDescription {
			VkImageViewType ImageType;
			int mip_start;
			int mip_count;
			int array_start;
			int array_count;
		};

		struct __Resource {
			__Device* device;
			bool IsBuffer;
			union {
				VkBufferCreateInfo BufferDescription;
				VkImageCreateInfo ImageDescription;
			};

			std::shared_ptr<__ResourceData> _Data;

			union {
				VkBufferView BufferView;
				VkImageView ImageView;
			};

			union {
				BufferSliceDescription BufferSlice;
				ImageSliceDescription ImageSlice;
			};

			__Resource(__Device* device, const VkImageCreateInfo& description, VkImage image, VkImageView view) :
				device(device),
				IsBuffer(false),
				ImageDescription(description),
				_Data(std::shared_ptr<__ResourceData>(new __ResourceData(device, image, nullptr))),
				ImageView(view)
			{
				ImageSlice.array_start = 0;
				ImageSlice.array_count = description.arrayLayers;
				ImageSlice.mip_start = 0;
				ImageSlice.mip_count = description.mipLevels;
			}

			~__Resource();
		};

		class CleaningProcess : public Process {

		public:
			EngineType RequiredEngines() override {
				return EngineType::NONE;
			}

			void Populate(goofy::CommandListManager manager) override
			{
			}
		};

		struct __Pipeline {};

		struct __Device {
			// Vulkan objects
			VkInstance _Instance = nullptr;
			VkSurfaceKHR _Surface = nullptr;
			VkPhysicalDevice _PhysicalDevice = nullptr;
			VkDevice _Device = nullptr;
			VkSwapchainKHR _Swapchain = nullptr;

			// Frames and async info
			int _FrameIndex;
			int _NumberOfFrames;
			int _NumberOfAsyncThreadsInFrame;
			int _NumberOfAsyncThreads;

			// Swapchain state
			unsigned int _ImageIndex;
			VkExtent2D _RT_Resolution;
			std::vector<Texture2D> _RenderTargets;

			// Window
			std::shared_ptr<__Window> _Window = nullptr;

			VkFormat PresentationFormat;
			std::vector<VkSemaphore> ImageReadyToRender;
			std::vector<VkSemaphore> ImageReadyToPresent;

			std::vector<__EngineManager*> _Engines; // One engine for each Family Queue: Present, Transfer, Compute, Graphics

			std::vector<std::thread> _OompaLoompas;
			bool _disposed = false;

			int _engine_mapping[16] = { -1, -1, -1, -1, -1, -1, -1, -1,-1, -1, -1, -1,-1, -1, -1, -1 };

			std::shared_ptr<ProducerConsumerQueue<std::shared_ptr<WorkPiece>>> _AsyncProcesses;
			std::shared_ptr<ProducerConsumerQueue<std::shared_ptr<WorkPiece>>> _FrameAsyncProcesses;

			int __MainRenderingEngineIndex;
			int __PresentingEngineIndex;

			inline int NumberOfFrames() {
				return _NumberOfFrames;
			}

			void __create_vk_instance(const PresenterDescription& description) {
				// Create Window if necessary
				switch (description.mode) {
				case PresenterCreationMode::NEW_GLFW_WINDOW:
				case PresenterCreationMode::EXISTING_GLFW_WINDOW:
				{
					GLFWwindow* window = nullptr;
					if (description.mode == PresenterCreationMode::NEW_GLFW_WINDOW) {
						glfwInit();
						glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
						glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
						window = glfwCreateWindow(description.resolution.width, description.resolution.height, description.window_name.data(), nullptr, nullptr);
					}
					else {
						window = (GLFWwindow*)description.ExistingWindow;
					}

					int w, h;
					glfwGetWindowSize(window, &w, &h);
					_RT_Resolution = { (unsigned int)w, (unsigned int)h };

					this->_Window = std::shared_ptr<__Window>(new __Window());
					this->_Window->IsGLFW = true;
					this->_Window->pWindow = window;
					break;
				}
				default:
					throw std::runtime_error("Not supported surface creation mode");
				}

				// Creating Vulkan Instance
				VkApplicationInfo appInfo{};
				appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
				appInfo.pApplicationName = description.window_name.data();
				appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
				appInfo.pEngineName = nullptr;
				appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
				appInfo.apiVersion = VK_API_VERSION_1_0;

				uint32_t extensionCount = 0;                // Getting available extensions
				const char** extensions = nullptr;

				switch (description.mode)
				{
				case PresenterCreationMode::EXISTING_GLFW_WINDOW:
				case PresenterCreationMode::NEW_GLFW_WINDOW:
					extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
					break;
				case PresenterCreationMode::OFFLINE:
					// No extensions here?
					break;
				default:
					throw std::runtime_error("Not supported");
					break;
				}

				VkInstanceCreateInfo instanceCreateInfo{};
				instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
				instanceCreateInfo.pApplicationInfo = &appInfo;
				instanceCreateInfo.enabledExtensionCount = extensionCount;
				instanceCreateInfo.ppEnabledExtensionNames = extensions;

				if (vkCreateInstance(&instanceCreateInfo, nullptr, &_Instance) != VK_SUCCESS) {
					throw std::runtime_error("failed to create instance!");
				}
			}

			void __create_vk_surface_from_glfw(GLFWwindow* window) {
				// Creating a surface for a window (IN WINDOWS!)
				if (glfwCreateWindowSurface(_Instance, window, nullptr, &_Surface) != VK_SUCCESS) {
					throw std::runtime_error("failed to create window surface!");
				}
			}

			void __create_vk_surface(const PresenterDescription& description) {
				switch (description.mode) {
				case PresenterCreationMode::OFFLINE:
					_Surface = nullptr; // No surface in offline case?
					break;
				case PresenterCreationMode::NEW_GLFW_WINDOW:
				case PresenterCreationMode::EXISTING_GLFW_WINDOW:
				{
					__create_vk_surface_from_glfw((GLFWwindow*)_Window->pWindow);
					break;
				}
				default:
					throw std::runtime_error("Not supported surface creation mode");
				}
			}

			int __minimal_queue_index_for(VkQueueFlagBits bits, bool require_present_support) {
				// Find queue families
				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(_PhysicalDevice, &queueFamilyCount, nullptr);

				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

				unsigned int minFlag = 0xFFFFFFFF;
				int QueueFamilyIndex = -1;

				int i = 0;
				for (const auto& queueFamily : queueFamilies) {
					VkBool32 presentSupport = false;
					vkGetPhysicalDeviceSurfaceSupportKHR(_PhysicalDevice, i, _Surface, &presentSupport);

					if ((queueFamily.queueFlags & bits) == bits && (!require_present_support || presentSupport)) {
						if (minFlag > (unsigned int)queueFamily.queueFlags)
						{
							QueueFamilyIndex = i;
							minFlag = (unsigned int)queueFamily.queueFlags;
						}
					}

					i++;
				}

				return QueueFamilyIndex;
			}

			void __create_vk_physical_device() {
				// Selecting a physical device
				uint32_t deviceCount = 0;
				vkEnumeratePhysicalDevices(_Instance, &deviceCount, nullptr);
				if (deviceCount == 0) {
					throw std::runtime_error("failed to find GPUs with Vulkan support!");
				}
				std::vector<VkPhysicalDevice> devices(deviceCount);
				vkEnumeratePhysicalDevices(_Instance, &deviceCount, devices.data());

				for (const auto& device : devices) {
					VkPhysicalDeviceProperties deviceProperties;
					VkPhysicalDeviceFeatures deviceFeatures;
					vkGetPhysicalDeviceProperties(device, &deviceProperties);
					vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
					if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
						_PhysicalDevice = device;
						break;
					}
				}
			}

			void ResolveEngineIndex(EngineType engines) {
				unsigned int bits = 0;
				if (((int)engines & (int)EngineType::TRANSFER) != 0) bits |= VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT;
				if (((int)engines & (int)EngineType::COMPUTE) != 0) bits |= VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT;
				if (((int)engines & (int)EngineType::GRAPHICS) != 0) bits |= VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT;
				if (((int)engines & (int)EngineType::RAYTRACING) != 0) bits |= VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT;

				int minimal_family = __minimal_queue_index_for((VkQueueFlagBits)bits, false);
				// save family in cache.
				_engine_mapping[(int)engines] = minimal_family;
			}

			EngineType GetSupportedEngines(VkQueueFlagBits bits) {
				int type = 0;
				if (bits & VkQueueFlagBits::VK_QUEUE_TRANSFER_BIT) type |= (int)EngineType::TRANSFER;
				if (bits & VkQueueFlagBits::VK_QUEUE_COMPUTE_BIT) type |= (int)EngineType::COMPUTE;
				if (bits & VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT) type |= (int)EngineType::GRAPHICS;
				if (bits & VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT) type |= (int)EngineType::RAYTRACING;
				return (EngineType)type;
			}

			void __create_presenter(const PresenterDescription& description) {
				// Create swap chain
				VkSwapchainCreateInfoKHR createInfo{};
				createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
				createInfo.surface = _Surface;
				createInfo.minImageCount = std::max(1, description.frames + 1);
				PresentationFormat = (VkFormat)description.PresentationFormat;
				createInfo.imageFormat = PresentationFormat;
				createInfo.imageColorSpace = VkColorSpaceKHR::VK_COLORSPACE_SRGB_NONLINEAR_KHR;
				createInfo.imageExtent = _RT_Resolution;
				createInfo.imageArrayLayers = 1;
				createInfo.imageUsage = __Convert(description.Usage);

				createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // RESOURCES CAN NOT BE SHARED BETWEEN ENGINES
				createInfo.queueFamilyIndexCount = 0; // Optional
				createInfo.pQueueFamilyIndices = nullptr; // Optional

				createInfo.preTransform = VkSurfaceTransformFlagBitsKHR::VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
				createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
				createInfo.presentMode = VkPresentModeKHR::VK_PRESENT_MODE_MAILBOX_KHR;
				createInfo.clipped = VK_TRUE;
				createInfo.oldSwapchain = VK_NULL_HANDLE;

				if (vkCreateSwapchainKHR(_Device, &createInfo, nullptr, &_Swapchain) != VK_SUCCESS) {
					throw std::runtime_error("failed to create swap chain!");
				}

				// Retrieve images from swapchain
				unsigned int imageCount;
				vkGetSwapchainImagesKHR(_Device, _Swapchain, &imageCount, nullptr);
				_RenderTargets.resize(imageCount);

				std::vector<VkImage> swapChainImages;
				swapChainImages.resize(imageCount);
				vkGetSwapchainImagesKHR(_Device, _Swapchain, &imageCount, swapChainImages.data());

				// Create image views
				std::vector<VkImageView> swapChainImageViews;
				swapChainImageViews.resize(imageCount);
				for (int i = 0; i < imageCount; i++)
				{
					VkImageViewCreateInfo ivcreateInfo{};
					ivcreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
					ivcreateInfo.image = swapChainImages[i];
					ivcreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
					ivcreateInfo.format = PresentationFormat;
					ivcreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					ivcreateInfo.subresourceRange.baseMipLevel = 0;
					ivcreateInfo.subresourceRange.levelCount = 1;
					ivcreateInfo.subresourceRange.baseArrayLayer = 0;
					ivcreateInfo.subresourceRange.layerCount = 1;
					if (vkCreateImageView(_Device, &ivcreateInfo, nullptr, &swapChainImageViews[i]) != VK_SUCCESS) {
						throw std::runtime_error("failed to create image views!");
					}

					VkImageCreateInfo creationInfo = { };
					creationInfo.arrayLayers = 1;
					creationInfo.mipLevels = 1;
					creationInfo.format = PresentationFormat;
					creationInfo.imageType = VK_IMAGE_TYPE_2D;
					creationInfo.extent = VkExtent3D{ _RT_Resolution.width, _RT_Resolution.height, 1 };

					_RenderTargets[i].__state = std::shared_ptr<__Resource>(new __Resource(this, creationInfo, swapChainImages[i], swapChainImageViews[i]));
				}

				//// Create Render Pass
				//VkAttachmentDescription colorAttachment{};
				//colorAttachment.format = PresentationFormat;
				//colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
				//colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				//colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				//colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				//colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				//colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				//colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

				//VkAttachmentReference colorAttachmentRef{};
				//colorAttachmentRef.attachment = 0;
				//colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

				//VkSubpassDescription subpass{};
				//subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
				//subpass.colorAttachmentCount = 1;
				//subpass.pColorAttachments = &colorAttachmentRef;

				//VkSubpassDependency dependency{};
				//dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
				//dependency.dstSubpass = 0;
				//dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				//dependency.srcAccessMask = 0;
				//dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				//dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

				//VkRenderPassCreateInfo renderPassInfo{};
				//renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
				//renderPassInfo.attachmentCount = 1;
				//renderPassInfo.pAttachments = &colorAttachment;
				//renderPassInfo.subpassCount = 1;
				//renderPassInfo.pSubpasses = &subpass;
				//renderPassInfo.dependencyCount = 1;
				//renderPassInfo.pDependencies = &dependency;

				//if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				//	throw std::runtime_error("failed to create render pass!");
				//}

				//// Create Framebuffers
				//swapChainFramebuffers.resize(swapChainImageViews.size());

				//for (size_t i = 0; i < swapChainImageViews.size(); i++) {
				//	VkImageView attachments[] = {
				//		swapChainImageViews[i]
				//	};

				//	VkFramebufferCreateInfo framebufferInfo{};
				//	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
				//	framebufferInfo.renderPass = renderPass;
				//	framebufferInfo.attachmentCount = 1;
				//	framebufferInfo.pAttachments = attachments;
				//	framebufferInfo.width = PresentationResolution.width;
				//	framebufferInfo.height = PresentationResolution.height;
				//	framebufferInfo.layers = 1;

				//	if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
				//		throw std::runtime_error("failed to create framebuffer!");
				//	}
				//}
				ImageReadyToRender.resize(NumberOfFrames());
				ImageReadyToPresent.resize(NumberOfFrames());
				for (int i = 0; i < NumberOfFrames(); i++) {
					// Create semaphores for presentation
					VkSemaphoreCreateInfo semaphoreInfo{};
					semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
					if (vkCreateSemaphore(_Device, &semaphoreInfo, nullptr, &ImageReadyToRender[i]) != VK_SUCCESS ||
						vkCreateSemaphore(_Device, &semaphoreInfo, nullptr, &ImageReadyToPresent[i]) != VK_SUCCESS) {
						throw std::runtime_error("failed to create semaphores!");
					}
				}
			}

			void __create_vk_device(const PresenterDescription& description) {

				int total_threads = 1 + description.frame_threads + description.async_threads;

				_FrameIndex = 0;
				_NumberOfFrames = std::max(1, description.frames);
				_NumberOfAsyncThreadsInFrame = description.frame_threads;
				_NumberOfAsyncThreads = description.async_threads;

				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(_PhysicalDevice, &queueFamilyCount, nullptr);

				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(_PhysicalDevice, &queueFamilyCount, queueFamilies.data());

				float queuePriority[16] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,1.0f };

				// Creating device and queues
				VkDeviceQueueCreateInfo* queueCreateInfos = new VkDeviceQueueCreateInfo[queueFamilyCount];

				for (int i = 0; i < queueFamilyCount; i++) {
					VkDeviceQueueCreateInfo queueCreateInfo{};
					queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queueCreateInfo.queueFamilyIndex = i;
					queueCreateInfo.queueCount = std::min((int)queueFamilies[i].queueCount, total_threads);
					queueCreateInfo.pQueuePriorities = queuePriority;
					queueCreateInfos[i] = queueCreateInfo;
				}

				VkPhysicalDeviceFeatures deviceFeatures{
				};

				const std::vector<const char*> deviceExtensions = {
					VK_KHR_SWAPCHAIN_EXTENSION_NAME
				};

				VkDeviceCreateInfo deviceCreateInfo{};
				deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
				deviceCreateInfo.pQueueCreateInfos = queueCreateInfos;
				deviceCreateInfo.queueCreateInfoCount = queueFamilyCount;
				deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
				deviceCreateInfo.enabledLayerCount = 0;
				deviceCreateInfo.enabledExtensionCount = deviceExtensions.size();
				deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

				if (vkCreateDevice(_PhysicalDevice, &deviceCreateInfo, nullptr, &_Device) != VK_SUCCESS) {
					throw std::runtime_error("failed to create logical device!");
				}

				__create_presenter(description);

				_Engines.resize(queueFamilyCount);
				for (int i = 0; i < queueFamilyCount; i++)
				{
					auto supportedEngines = GetSupportedEngines((VkQueueFlagBits)queueFamilies[i].queueFlags);
					_Engines[i] = new __EngineManager(_Device, i, supportedEngines, _NumberOfFrames, _NumberOfAsyncThreadsInFrame, _NumberOfAsyncThreads, std::min((int)queueFamilies[i].queueCount, total_threads));
				}

				for (int i = 0; i < 16; i++)
					ResolveEngineIndex((EngineType)i);

				__MainRenderingEngineIndex = __minimal_queue_index_for(VkQueueFlagBits::VK_QUEUE_GRAPHICS_BIT, false);
				__PresentingEngineIndex = __minimal_queue_index_for((VkQueueFlagBits)0, true);

				_FrameAsyncProcesses = std::shared_ptr<ProducerConsumerQueue<std::shared_ptr<WorkPiece>>>(new ProducerConsumerQueue<std::shared_ptr<WorkPiece>>(description.frame_threads * 2));
				_AsyncProcesses = std::shared_ptr<ProducerConsumerQueue<std::shared_ptr<WorkPiece>>>(new ProducerConsumerQueue<std::shared_ptr<WorkPiece>>(description.async_threads * 2));

				delete[] queueCreateInfos;
			}

			std::shared_ptr<WorkPiece> __CreateWorkPiece(std::shared_ptr<Process> process, DispatchMode mode) {
				// Retrieve engine type to enqueue to
				int engineIndex = _engine_mapping[(int)process->RequiredEngines()];
				std::shared_ptr<WorkPiece> workPiece = std::shared_ptr<WorkPiece>(new WorkPiece());
				workPiece->GraphicProcess = process;
				workPiece->Dispatch = mode;
				workPiece->State = WorkPieceState::DISPATCHED;
				workPiece->EngineIndex = engineIndex;
				workPiece->ManagerIndex = -1; // Not asigned any manager yet.
				return workPiece;
			}

			void __PerformPopulation(std::shared_ptr<WorkPiece> workPiece, int threadIdx) {
				int managerIdx = 0;
				switch (workPiece->Dispatch) {
				case DispatchMode::MAIN_THREAD:
					assert(threadIdx == 0);
					managerIdx = _FrameIndex * (_NumberOfAsyncThreadsInFrame + 1);
					break;
				case DispatchMode::ASYNC_FRAME:
					assert(threadIdx >= 1 && threadIdx <= _NumberOfAsyncThreadsInFrame);
					managerIdx = _FrameIndex * (_NumberOfAsyncThreadsInFrame + 1) + threadIdx;
					break;
				case DispatchMode::ASYNC:
					assert(threadIdx > _NumberOfAsyncThreadsInFrame && threadIdx < 1 + _NumberOfAsyncThreadsInFrame + _NumberOfAsyncThreads);
					managerIdx = (_NumberOfFrames - 1) * (_NumberOfAsyncThreadsInFrame + 1) + threadIdx;
					break;
				}
				workPiece->ManagerIndex = managerIdx;
				if (workPiece->EngineIndex >= 0)
					_Engines[workPiece->EngineIndex]->Dispatch(workPiece);
			}

			static void __OompaLoompaWork(__Device* _this, int idx) {
				std::cout << "Created worker " << idx << std::endl;
				while (!_this->_disposed) {
					// Do work here...
					std::shared_ptr<WorkPiece> workPiece = idx <= _this->_NumberOfAsyncThreadsInFrame ? _this->_FrameAsyncProcesses->Consume() : _this->_AsyncProcesses->Consume();
					_this->__PerformPopulation(workPiece, idx);
				}
				std::cout << "Finished worker " << idx << std::endl;
			}

			void __create_scheduler(const PresenterDescription& description) {
				for (int i = 1; i <= description.frame_threads + description.async_threads; i++)
					_OompaLoompas.push_back(std::thread(__OompaLoompaWork, this, i));
			}

			__Device(const PresenterDescription& description) {
				__create_vk_instance(description);
				__create_vk_surface(description);
				__create_vk_physical_device();
				__create_vk_device(description);
				__create_scheduler(description);
			}

			~__Device() {
				_disposed = true;
				std::shared_ptr<Process> cleaning = std::shared_ptr<Process>(new CleaningProcess());
				for (int i = 0; i < _NumberOfAsyncThreads; i++)
				{
					auto t = Dispatch(cleaning, DispatchMode::ASYNC);
					Flush(1, &t, 0, nullptr)->Wait();
				}
				for (int i = 0; i < _NumberOfAsyncThreadsInFrame; i++)
				{
					auto t = Dispatch(cleaning, DispatchMode::ASYNC_FRAME);
					Flush(1, &t, 0, nullptr)->Wait();
				}
				for (int i = 0; i < _OompaLoompas.size(); i++)
					_OompaLoompas[i].join();
				_OompaLoompas.clear(); // join all threads
				_RenderTargets.clear(); // Destroy all RTs objects
				for (int i = 0; i < _Engines.size(); i++)
					delete _Engines[i];
				if (_Swapchain) vkDestroySwapchainKHR(_Device, _Swapchain, nullptr);
				if (_Device) vkDestroyDevice(_Device, nullptr);
				if (_Surface) vkDestroySurfaceKHR(_Instance, _Surface, nullptr);
				if (_Instance) vkDestroyInstance(_Instance, nullptr);
			}

			std::shared_ptr<__CPUTask> Dispatch(std::shared_ptr<Process> process, DispatchMode mode) {
				// Redirect if threads not available
				switch (mode)
				{
				case goofy::DispatchMode::ASYNC_FRAME:
				{
					if (_NumberOfAsyncThreadsInFrame == 0)
						return Dispatch(process, DispatchMode::MAIN_THREAD);
					break;
				}
				case goofy::DispatchMode::ASYNC:
					if (_NumberOfAsyncThreads == 0)
					{
						if (_NumberOfAsyncThreadsInFrame == 0)
							return Dispatch(process, DispatchMode::MAIN_THREAD);
						else
							return Dispatch(process, DispatchMode::ASYNC_FRAME);
					}
					break;
				}

				auto workPiece = __CreateWorkPiece(process, mode);
				std::shared_ptr<__CPUTask> task = std::shared_ptr<states::__CPUTask>(new states::__CPUTask());
				task->workPiece = workPiece;

				switch (mode)
				{
				case goofy::DispatchMode::MAIN_THREAD:
				{
					__PerformPopulation(workPiece, 0);
					break;
				}
				case goofy::DispatchMode::ASYNC_FRAME:
				{
					_FrameAsyncProcesses->Produce(workPiece);
					break;
				}
				case goofy::DispatchMode::ASYNC:
					_AsyncProcesses->Produce(workPiece);
					break;
				default:
					break;
				}

				return task;
			}

			std::shared_ptr<__GPUTask> Flush(int count, std::shared_ptr<__CPUTask>* tasks, int waitingCount, std::shared_ptr<__GPUTask>* waitingGPU) {
				// Wait for completation on the CPU.
				for (int i = 0; i < count; i++)
				{
					tasks[i]->Wait();
					_Engines[tasks[i]->workPiece->EngineIndex]->MarkForFlush(tasks[i]->workPiece->ManagerIndex);
				}

				__GPUTask* result = new __GPUTask();
				
				for (auto e : _Engines)
					e->FlushMarked(waitingCount, waitingGPU, result->children);
				
				return std::shared_ptr<__GPUTask>(result);
			}
		};

		
	}
}

#endif