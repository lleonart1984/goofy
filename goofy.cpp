

#include "goofy.states.h"

namespace goofy {

	Device::Device(states::__Device* initialState) {
		this->__state = std::shared_ptr<states::__Device>(initialState);
	}

	inline int Device::GetCurrentFrameIndex()
	{
		return __state->_FrameIndex;
	}

	inline int Device::NumberOfFrames() {
		return __state->NumberOfFrames();
	}

	inline int Device::RenderTargetWidth()
	{
		return __state->_RT_Resolution.width;
	}

	inline int Device::RenderTargetHeight()
	{
		return __state->_RT_Resolution.height;
	}

	Texture2D Device::GetCurrentRenderTarget()
	{
		return __state->_RenderTargets[__state->_ImageIndex];
	}

	void goofy::Device::BindTechnique(std::shared_ptr<Technique> technique)
	{
		technique->__state = this->__state;
	}

	CPUTask Device::Dispatch(std::shared_ptr<Process> process, DispatchMode mode) {

		CPUTask task;
		task.__state = this->__state->Dispatch(process, mode);
		return task;
	}

	GPUTask Device::Flush(int count, CPUTask* tasks, int waitingCount, GPUTask* waitingGPU)
	{
		return GPUTask{
			__state->Flush(
				count,
				(std::shared_ptr<goofy::states::__CPUTask>*) tasks,
				waitingCount,
				(std::shared_ptr<goofy::states::__GPUTask>*) waitingGPU
			) };
	}

	Presenter::Presenter(const PresenterDescription& description):Device(new states::__Device(description)) {
	}

	void goofy::Presenter::BeginFrame()
	{
		for (goofy::states::__EngineManager* e : __state->_Engines)
			e->WaitForCompletition(__state->_FrameIndex); // auto submit all pending work

		// Get Index of the current target in swapchain
		vkAcquireNextImageKHR(__state->_Device, __state->_Swapchain, UINT64_MAX, __state->ImageReadyToRender[__state->_FrameIndex], VK_NULL_HANDLE, &__state->_ImageIndex);

		// Enqueue signaling for waiting for image to be ready.
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore waitSemaphores[] = { __state->ImageReadyToRender[__state->_FrameIndex] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 0;
		submitInfo.pCommandBuffers = nullptr;
		submitInfo.signalSemaphoreCount = 0;
		submitInfo.pSignalSemaphores = nullptr;
		// Render
		if (vkQueueSubmit(__state->_Engines[__state->__MainRenderingEngineIndex]->Queues[0], 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit waiting command buffer!");
		}
	}

	void Presenter::EndFrame()
	{
		for (goofy::states::__EngineManager* e : __state->_Engines)
			e->Flush(__state->_FrameIndex); // auto submit all pending work

		// Enqueue signaling for waiting for image to be ready to present.
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		VkSemaphore signalSemaphores[] = { __state->ImageReadyToPresent[__state->_FrameIndex] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;
		// Render
		if (vkQueueSubmit(__state->_Engines[__state->__MainRenderingEngineIndex]->Queues[0], 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit waiting command buffer!");
		}
		// Present 
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;
		VkSwapchainKHR swapChains[] = { __state->_Swapchain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &__state->_ImageIndex;
		presentInfo.pResults = nullptr; // Optional
		vkQueuePresentKHR(__state->_Engines[__state->__PresentingEngineIndex]->Queues[0], &presentInfo);

		__state->_FrameIndex = (__state->_FrameIndex + 1) % __state->_NumberOfFrames;
	}

	void Presenter::CreateNew(const PresenterDescription& description, std::shared_ptr<Presenter>& presenter)
	{
		presenter = std::shared_ptr<Presenter>(new Presenter(description));
	}

	Window Presenter::Window()
	{
		auto w = goofy::Window();
		w.__state = this->__state->_Window;
		return w;
	}
	
	CommandListManager::CommandListManager(EngineType support):_supported_engines(support)
	{
	}

	EngineType CommandListManager::Engines()
	{
		return this->_supported_engines;
	}

	void goofy::GraphicsManager::Clear(Image2D image, const Formats::R32G32B32A32_SFLOAT &color)
	{
		VkCommandBuffer cmdList = this->__state->vkCmdList;
		VkClearColorValue v = { color.R, color.G, color.B, color.A };
		std::shared_ptr<goofy::states::__Resource> state = image.__state;
		std::shared_ptr<goofy::states::__ResourceData> data = state->_Data;
		VkImageSubresourceRange range;
		range.baseMipLevel = state->ImageSlice.mip_start;
		range.levelCount = state->ImageSlice.mip_count;
		range.baseArrayLayer = state->ImageSlice.array_start;
		range.layerCount = state->ImageSlice.array_count;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		vkCmdClearColorImage(cmdList, data->Image, VkImageLayout::VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
	}

	void CPUTask::Wait() {
		__state->Wait();
	}

	void GPUTask::Wait() {
		__state->Wait();
	}

	GPUTask GPUTask::Combine(int count, GPUTask* tasks)
	{
		return GPUTask{ goofy::states::__GPUTask::Union(count, (std::shared_ptr<goofy::states::__GPUTask>*) tasks) };
	}

	bool Window::IsClosed()
	{
		if (__state->IsGLFW) {
			GLFWwindow* window = (GLFWwindow*)__state->pWindow;
			return glfwWindowShouldClose(window);
		}
		else {
			throw std::runtime_error("Unsupported SDL windows");
		}
	}
	
	void Window::PollEvents()
	{
		if (__state->IsGLFW) {
			glfwPollEvents();
		}
		else {
			throw std::runtime_error("Unsupported SDL windows");
		}
	}
	
	void* Window::InternalWindow()
	{
		return __state->pWindow;
	}

	double Window::Time() {
		if (__state->IsGLFW) {
			return glfwGetTime();
		}
		else {
			throw std::runtime_error("Unsupported SDL windows");
		}
	}

	namespace Formats {
		
		FormatHandle R8G8B8A8::SRGB_Handle() 
		{
			return (FormatHandle)VkFormat::VK_FORMAT_R8G8B8A8_SRGB;
		}

		FormatHandle R32G32B32A32_SFLOAT::Handle()
		{
			return (FormatHandle)VkFormat::VK_FORMAT_R32G32B32A32_SFLOAT;
		}
	
	}
}