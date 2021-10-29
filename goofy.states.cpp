#include "goofy.states.h"

namespace goofy {
	namespace states {

		VkImageUsageFlagBits __Convert(const ImageUsage& usage) {
			int bits = 0;
			if (usage.TransferSource) bits |= (int)VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			if (usage.TransferDestination) bits |= (int)VkImageUsageFlagBits::VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			if (usage.Storage) bits |= (int)VkImageUsageFlagBits::VK_IMAGE_USAGE_STORAGE_BIT;
			if (usage.Sampled) bits |= (int)VkImageUsageFlagBits::VK_IMAGE_USAGE_SAMPLED_BIT;
			if (usage.RenderTarget) bits |= (int)VkImageUsageFlagBits::VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
			if (usage.DepthStencil) bits |= (int)VkImageUsageFlagBits::VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			return (VkImageUsageFlagBits)bits;
		}

		WorkPiece::WorkPiece() { }

		void WorkPiece::PopulationCompleted() {
			mutex.lock();
			State = WorkPieceState::POPULATION_COMPLETED;
			AfterPopulated.Done();
			mutex.unlock();
		}

		inline void WorkPiece::WaitForPopulation() {
			AfterPopulated.Wait();
		}

		inline bool WorkPiece::HasBeenSubmitted() {
			return State == WorkPieceState::SUBMITTED;
		}

		__ResourceData::~__ResourceData()
		{
			if (Memory) { // Only owned resources should be destroyed.
				if (IsBuffer)
					vkDestroyBuffer(device->_Device, Buffer, nullptr);
				else
					vkDestroyImage(device->_Device, Image, nullptr);
			}
			if (UploadingStaging)
				vkDestroyBuffer(device->_Device, UploadingStaging, nullptr);
			if (DownloadingStaging)
				vkDestroyBuffer(device->_Device, DownloadingStaging, nullptr);
		}

		__Resource::~__Resource() {
			if (IsBuffer)
			{
				if (BufferView)
					vkDestroyBufferView(device->_Device, BufferView, nullptr);
			}
			else
			{
				if (ImageView)
					vkDestroyImageView(device->_Device, ImageView, nullptr);
			}
		}

		__GPUTask::__GPUTask() {
			this->device = nullptr;
			this->GPUFinished = nullptr;
			this->finished = false;
		}

		__GPUTask::~__GPUTask() {
			if (GPUFinished) 
				vkDestroySemaphore(device, GPUFinished, nullptr);
		}

		void __GPUTask::Wait() {
			if (finished)
				return;

			if (GPUFinished != nullptr) {
				VkSemaphoreWaitInfo info = {};
				uint64_t value = 1;
				info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
				info.pSemaphores = &GPUFinished;
				info.semaphoreCount = 1;
				info.pValues = &value;
				vkWaitSemaphores(device, &info, UINT64_MAX);
			}

			for (std::shared_ptr<__GPUTask> t : children)
				t->Wait();

			finished = true;
		}

		std::shared_ptr<__GPUTask> __GPUTask::CreateSingle(VkDevice device, bool empty) {
			std::shared_ptr<__GPUTask> task = std::shared_ptr<__GPUTask>(new __GPUTask());
			task->device = device;
			if (!empty) {
				VkSemaphoreCreateInfo info = {};
				info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

				vkCreateSemaphore(device, &info, nullptr, &task->GPUFinished);
			}
			task->finished = empty;
			return task;
		}

		std::shared_ptr<__GPUTask> __GPUTask::Union(int count, std::shared_ptr<__GPUTask>* tasks) {
			std::shared_ptr<__GPUTask> task = std::shared_ptr<__GPUTask>(new __GPUTask());
			task->device = tasks[0]->device;
			for (int i = 0; i < count; i++)
				if (!tasks[i]->finished)
					task->children.push_back(tasks[i]);
			task->finished = task->children.size() > 0;
			return task;
		}

		void __GPUTask::FillSemaphores(std::vector<VkSemaphore>& semaphores) {
			if (finished)
				return;

			if (GPUFinished != nullptr)
				semaphores.push_back(GPUFinished);

			for (std::shared_ptr<__GPUTask> c : children)
				if (!c->finished)
					c->FillSemaphores(semaphores);
		}

		void __CPUTask::Wait() {
			workPiece->WaitForPopulation();
		}

		void __CommandListManager::__Open() {
			if (State == CommandListState::Recording)
				return;

			if (State != CommandListState::Initial)
				throw std::runtime_error("Fail to open an executing command list");

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0; // Optional
			beginInfo.pInheritanceInfo = nullptr; // Optional

			if (vkBeginCommandBuffer(vkCmdList, &beginInfo) != VK_SUCCESS) {
				throw std::runtime_error("failed to begin recording command buffer!");
			}

			State = CommandListState::Recording;
		}

		void __CommandListManager::__Close() {
			if (State != CommandListState::Recording)
				throw std::runtime_error("Closing a command buffer has not been opened");

			if (vkEndCommandBuffer(vkCmdList) != VK_SUCCESS) {
				throw std::runtime_error("failed to record command buffer!");
			}

			State = CommandListState::Executable;
		}

		void __CommandListManager::__Reset() {
			if (State == CommandListState::OnGPU)
				throw std::runtime_error("Reseting a command list has not finished on the gpu");

			vkResetCommandBuffer(vkCmdList, VkCommandBufferResetFlagBits::VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

			State = CommandListState::Initial;
		}

		__CommandQueueManager::__CommandQueueManager(VkDevice device, int familyIndex, EngineType supported, VkQueue queue, bool throwErrorIfAbandonedTasks) : 
			SupportedEngines(supported), 
			device(device), 
			queue(queue),
			throwErrorIfAbandonedTasks(throwErrorIfAbandonedTasks)
		{
			// Create command pool and allocate
			VkCommandPoolCreateInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			info.queueFamilyIndex = familyIndex;
			vkCreateCommandPool(device, &info, nullptr, &pool);
		}

		__CommandQueueManager::~__CommandQueueManager() {
			vkDestroyCommandPool(device, pool, nullptr);
		}

		std::shared_ptr<__CommandListManager> __CommandQueueManager::FetchNew() {
			std::shared_ptr<__CommandListManager> result;

			if (reusableCmdBuffers.size() > 1)
			{
				result = reusableCmdBuffers.back();
				reusableCmdBuffers.pop_back();
			}
			else {
				result = std::shared_ptr<__CommandListManager>(new __CommandListManager());
				result->SupportedEngines = SupportedEngines;

				VkCommandBufferAllocateInfo info = { };
				info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				info.commandPool = pool;
				info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
				info.commandBufferCount = 1;

				vkAllocateCommandBuffers(device, &info, &result->vkCmdList);
			}

			result->__Open();

			return result;
		}

		/// <summary>
		/// Gets a command list buffer ready to be populated.
		/// </summary>
		/// <returns></returns>
		std::shared_ptr<__CommandListManager> __CommandQueueManager::Peek() {
			if (recordingBuffer == nullptr)
				recordingBuffer = FetchNew();
			assert(recordingBuffer != nullptr);
			return recordingBuffer;
		}

		void __CommandQueueManager::WaitForPopulation() {
			sync_populated.lock();
			for (int i=0; i< populated.size(); i++)
				populated[i]->WaitForPopulation();
			sync_populated.unlock();
		}

		/// <summary>
		/// Submit current recording command buffer to the gpu.
		/// </summary>
		std::shared_ptr<__GPUTask> __CommandQueueManager::SubmitCurrent(int count, std::shared_ptr<__GPUTask>* wait_for) {
			sync_populated.lock();

			if (recordingBuffer == nullptr)
			{
				sync_populated.unlock();
				return __GPUTask::CreateSingle(device, true);
			}

			std::shared_ptr<__GPUTask> task = __GPUTask::CreateSingle(device, false);

			recordingBuffer->__Close();

			waitingSemaphores.clear();
			waintingStages.clear();

			VkSubmitInfo sinfo = {};
			sinfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			sinfo.commandBufferCount = 1;
			sinfo.pCommandBuffers = &recordingBuffer->vkCmdList;

			for (int i = 0; i < count; i++)
				if (!wait_for[i]->finished)
					wait_for[i]->FillSemaphores(waitingSemaphores);

			waintingStages.resize(waitingSemaphores.size());
			for (int i = 0; i < waintingStages.size(); i++)
				waintingStages[i] = VkPipelineStageFlagBits::VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

			sinfo.pWaitDstStageMask = waintingStages.data();
			sinfo.waitSemaphoreCount = waitingSemaphores.size();
			sinfo.pWaitSemaphores = waitingSemaphores.data();

			sinfo.signalSemaphoreCount = 1;
			sinfo.pSignalSemaphores = &task->GPUFinished;
			vkQueueSubmit(queue, 1, &sinfo, nullptr);

			submittedBuffers.push_back(recordingBuffer);
			submittedTasks.push_back(task);

			for (std::shared_ptr<WorkPiece> w : populated)
				w->State = WorkPieceState::SUBMITTED;
			populated.clear();

			recordingBuffer = nullptr;

			sync_populated.unlock();

			return task;
		}

		/// <summary>
		/// Wait for all submitted tasks. This method should be called before starting a frame using this command pool manager.
		/// </summary>
		void __CommandQueueManager::WaitForPendings() {
			waitingSemaphores.resize(submittedTasks.size());
			waitingValues.resize(submittedTasks.size(), 1);
			int total = 0;
			for (int i = 0; i < submittedTasks.size(); i++)
				if (!submittedTasks[i]->finished)
					waitingSemaphores[total++] = submittedTasks[i]->GPUFinished;
			VkSemaphoreWaitInfo info = {};
			info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
			info.pSemaphores = waitingSemaphores.data();
			info.pValues = waitingValues.data();
			info.semaphoreCount = total;
			vkWaitSemaphores(device, &info, UINT64_MAX);
			for (int i = 0; i < submittedBuffers.size(); i++)
			{
				submittedTasks[i]->finished = true;
				submittedBuffers[i]->__Reset();
				reusableCmdBuffers.push_back(submittedBuffers[i]);
			}
			submittedBuffers.clear();
			submittedTasks.clear();
		}

		void __CommandQueueManager::Clean() {
			int i = 0;
			while (i < submittedTasks.size())
			{
				if (submittedTasks[i]->finished)
				{
					submittedBuffers[i]->__Reset();
					reusableCmdBuffers.push_back(submittedBuffers[i]);
					submittedBuffers[i] = submittedBuffers.back();
					submittedTasks[i] = submittedTasks.back();
					submittedBuffers.pop_back();
					submittedTasks.pop_back();
				}
				else {
					if (submittedTasks[i].use_count() == 1)
						throw std::runtime_error("Async process submitted but abandoned! Please, keep the GPUTask alive and synchronize manually.");

					i++;
				}
			}

			for (std::shared_ptr<WorkPiece> w : populated)
			{
				if (w->Dispatch == DispatchMode::ASYNC && w.use_count() <= 2) // abandoned async populated task
					throw std::runtime_error("Abandoned async populating thread!");
			}
		}

		void __CommandQueueManager::Populating(std::shared_ptr<WorkPiece> task, std::shared_ptr<__CommandListManager> &cmdList) {
			sync_populated.lock();
			populated.push_back(task);
			cmdList = Peek();
			sync_populated.unlock();
		}

		__EngineManager::__EngineManager() { } // empty constructor for null initialization

		__EngineManager::__EngineManager(VkDevice device, int familyIndex, EngineType supportedEngines, int frames, int frame_async_threads, int async_threads, int queues) :
			device(device),
			frames(frames),
			frame_async_threads(frame_async_threads),
			async_threads(async_threads),
			supportedEngines(supportedEngines) {
			this->Queues.resize(queues);
			this->Managers.resize(frames * (frame_async_threads + 1) + async_threads);

			for (int i = 0; i < queues; i++)
				vkGetDeviceQueue(device, familyIndex, i, &Queues[i]);

			for (int i = 0; i < Managers.size(); i++)
			{
				bool isAsynThread = i >= frames * (frame_async_threads + 1);
				Managers[i] = std::shared_ptr<__CommandQueueManager>(new __CommandQueueManager(device, familyIndex, supportedEngines, Queues[i % queues], isAsynThread));
			}
			marked.resize(Managers.size());
		}

		__EngineManager::~__EngineManager() {
		}

		void __EngineManager::Dispatch(std::shared_ptr<WorkPiece> workPiece) {
			int cmdIdx = workPiece->ManagerIndex;
			
			std::shared_ptr<__CommandListManager> cmdList;
			Managers[cmdIdx]->Populating(workPiece, cmdList);

			if (cmdList == nullptr)
			{
				throw std::exception("Weird...");
			}
			goofy::CommandListManager wrapper(supportedEngines);
			wrapper.__state = cmdList;
			workPiece->GraphicProcess->Populate(wrapper);
			workPiece->PopulationCompleted();
		}

		void __EngineManager::Flush(int frame) {
			for (int i = 0; i < frame_async_threads + 1; i++)
				Managers[(frame_async_threads + 1) * frame + i]->WaitForPopulation();

			for (int i = 0; i < frame_async_threads + 1; i++)
				Managers[(frame_async_threads + 1) * frame + i]->SubmitCurrent(0, nullptr);
		}

		void __EngineManager::WaitForCompletition(int frame) {

			for (int i = 0; i < frame_async_threads + 1; i++)
				Managers[(frame_async_threads + 1) * frame + i]->WaitForPendings();

			for (int i = 0; i < async_threads; i++)
				Managers[(frame_async_threads + 1) * frames + i]->Clean();

		}

		void __EngineManager::CleanAsyncManagers() {

			for (int i = 0; i < async_threads; i++)
				Managers[(frame_async_threads + 1) * frames + i]->Clean();

		}

		void __EngineManager::MarkForFlush(int managerIdx)
		{
			marked[managerIdx] = true;
		}
		void __EngineManager::FlushMarked(int waiting, std::shared_ptr<__GPUTask>* waitingGPU, std::vector<std::shared_ptr<__GPUTask>>& tasks)
		{
			for (int i=0; i<Managers.size(); i++)
				if (marked[i]) {
					tasks.push_back(Managers[i]->SubmitCurrent(waiting, waitingGPU));
					marked[i] = false;
				}
		}
	}
}