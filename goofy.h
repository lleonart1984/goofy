#ifndef GOOFY_H
#define GOOFY_H

#include <memory>
#include <vector>
#include <iostream>

using namespace std;

#pragma region Prototypes

namespace goofy {

	// Public definitions
	class Device;
	class Presenter;
	class Technique;
	class Process;
	class BakedProcess;

	class Pipeline;
	class ComputePipeline;
	class GraphicsPipeline;
	class RaytracingPipeline;

	class Binder;
	class ComputeBinder;
	class GraphicsBinder;
	class RaytracingBinder;

	struct CommandListManager;
	struct TransferManager;
	struct ComputeManager;
	struct ComputeExclusiveManager;
	struct GraphicsManager;
	struct RaytracingManager;

	class Resource;
	class Buffer;
	class Image1D;
	class Image2D;
	class Image3D;
	class VertexBuffer;
	class IndexBuffer;
	class Texture1D;
	class Texture2D;
	class Texture3D;
	struct Sampler;

	// Sync objects
	struct CPUTask;
	struct GPUTask;
	struct Rallypoint;
	struct Barrier;

	enum class EngineType;
	enum class PipelineStage;
	enum class ResourceAccess;

	namespace states {
		struct __Window;
		struct __Device;
		struct __EngineManager;
		struct __Pipeline;
		struct __CommandListManager;
		struct __Resource;
		struct __CPUTask;
		struct __GPUTask;
		struct __Rallypoint;
		struct __Barrier;
	}
}

#pragma endregion

#pragma region Memory Management

namespace goofy {

	template<typename S>
	class Obj {
		friend states::__Device;
		friend states::__EngineManager;
		friend GPUTask;
		friend CPUTask;
		friend Presenter;
		friend Device;
		friend CommandListManager;
		friend GraphicsManager;
	protected:
		std::shared_ptr<S> __state = nullptr;
		Obj() {}
		Obj(std::shared_ptr<S> state) :__state(state) {}
	public:
		bool IsNull();
	};
}

#pragma endregion

#pragma region Enums

namespace goofy {

	/// <summary>
	/// Represents different ways to enqueue a process
	/// </summary>
	enum class DispatchMode : int {
		/// <summary>
		/// The process is enqueued synchronously in the main thread.
		/// This task is automatically submitted after frame finishes.
		/// </summary>
		MAIN_THREAD = 0,
		/// <summary>
		/// The process is set for an asynchronous population and enqueue in current frame.
		/// This task is automatically submitted after frame finishes.
		/// </summary>
		ASYNC_FRAME = 1,
		/// <summary>
		/// The process is set for an asynchronous population and enqueue.
		/// This task must explicitly flushed but can survive across frames.
		/// </summary>
		ASYNC = 2
	};

	/// <summary>
	/// Different engines supported. Each engine represents a subset of functionalities. 
	/// The Engines might be put together to represent the capabilities expected from the command list manager
	/// </summary>
	enum class EngineType {
		NONE = 0,
		TRANSFER = 1,
		COMPUTE = 2,
		GRAPHICS = 4,
		RAYTRACING = 8
	};

	enum class PresenterCreationMode {
		OFFLINE,
		NEW_GLFW_WINDOW,
		EXISTING_GLFW_WINDOW,
		NEW_SDL_WINDOW,
		EXISTING_SDL_WINDOW
	};

	enum class PipelineStage {
		TRANSFER,
		COMPUTE,
		VERTEX,
		GEOMETRY,
		FRAGMENT,
		TESSELLATION_HULL,
		TESSELLATION_DOMAIN
	};

	enum class ResourceAccess {
		NONE,
		READ,
		WRITE
	};
}

#pragma endregion

#pragma region Formats

namespace goofy {

	typedef long FormatHandle;

	namespace Formats {
		struct R8G8B8A8 {
			union {
				unsigned int Value;
				struct Components {
					char R;
					char G;
					char B;
					char A;
				} Components;
			};

			R8G8B8A8() :R8G8B8A8(0) {}
			R8G8B8A8(unsigned int value) { this->Value = value; }
			R8G8B8A8(char r, char g, char b, char a) {
				this->Components.R = r;
				this->Components.G = g;
				this->Components.B = b;
				this->Components.A = a;
			}

			static FormatHandle SRGB_Handle();
			static FormatHandle SNORM_Handle();
			static FormatHandle UNORM_Handle();
			static FormatHandle USCALED_Handle();
			static FormatHandle SSCALED_Handle();
		};

		struct R32G32B32A32_SFLOAT {
			float R;
			float G;
			float B;
			float A;

			R32G32B32A32_SFLOAT() :R32G32B32A32_SFLOAT(0, 0, 0, 0) {}
			R32G32B32A32_SFLOAT(float r, float g, float b, float a) : R(r), G(g), B(b), A(a) {
			}

			static FormatHandle Handle();
		};
	}

}

#pragma endregion

#pragma region Device

template<typename T>
T ___dr(T* a) { return a; }

namespace goofy {

	/// <summary>
	/// Defines different usages of an image.
	/// </summary>
	struct ImageUsage {
		/// <summary>
		/// Allows transfers from the image.
		/// </summary>
		bool TransferSource;
		/// <summary>
		/// Allows transfers to the images.
		/// </summary>
		bool TransferDestination;
		/// <summary>
		/// Allows the image to be used as a sampled texture.
		/// </summary>
		bool Sampled;
		/// <summary>
		/// Allows the image to be a storage image.
		/// </summary>
		bool Storage;
		/// <summary>
		/// Allows the image to be used in a framebuffer.
		/// </summary>
		bool RenderTarget;
		/// <summary>
		/// Allows the image to be used as depthstencil buffer.
		/// </summary>
		bool DepthStencil;
	};
	
	struct PresenterDescription {
		/// <summary>
		/// Determines the initial surface for the presenter to draw to.
		/// (Offline) means there is no a window and the framebuffer is a normal image with the specific resolution. 
		/// Swapchain is emulated in this case.
		/// (New_*) means it will create a new window with the resolution specified.
		/// (Extisting_*) means it will use a existing window to draw to. Resolution can not be specified and will use current window resolution.
		/// </summary>
		PresenterCreationMode mode;

		/// <summary>
		/// Determines the number of frames in fly for the presenter. 
		/// If 0 is specified then deafult value 1 is assumed.
		/// </summary>
		int frames;

		/// <summary>
		/// Determines the number of internal threads used for asynchronous command list population in frames.
		/// If 0 is specified then async calls will be solved synchronously.
		/// Process in these threads are automatically flushed every frame.
		/// </summary>
		int frame_threads;

		/// <summary>
		/// Determines the number of internal threads used for asynchronous command list population across frames.
		/// If 0 is specified then async calls will be solved synchronously.
		/// Process in these threads must be flushed by user. Loosing references to those CPU or GPU Task objects will produce an exception.
		/// </summary>
		int async_threads;

		/// <summary>
		/// Determines the presentation format for the framebuffer.
		/// Common value used is Format::R8G8B8A8_SRGB
		/// </summary>
		FormatHandle PresentationFormat;

		/// <summary>
		/// Determines the valid usages of swapchain images.
		/// </summary>
		ImageUsage Usage;

		/// <summary>
		/// Gets or sets the window name in case a new window is created.
		/// </summary>
		std::string window_name;

		union {
			/// <summary>
			/// Instance of GLFW window to draw to if EXISTING_GLFW_WINDOW
			/// Instance of SDL window to draw to if EXISTING_SDL_WINDOW
			/// </summary>
			void* ExistingWindow;

			struct Resolution {
				unsigned int width;
				unsigned int height;
			} resolution;
		};
	};

	/// <summary>
	/// Allows to define events for in-queue commands synchronization
	/// </summary>
	struct Rallypoint : public Obj<states::__Rallypoint>
	{
	};

	/// <summary>
	/// Allows to define barriers for in-queue commands synchronization
	/// </summary>
	struct Barrier : public Obj<states::__Barrier> {
	};

	struct CommandListManager : public Obj<states::__CommandListManager> {
		friend states::__EngineManager;
		friend TransferManager;
		friend ComputeManager;
		friend GraphicsManager;
		friend RaytracingManager;
	private:
		EngineType _supported_engines;
		CommandListManager(EngineType support);
		template<typename T>
		void CheckCastSupport();
	public:
		template<typename T>
		/// <summary>
		/// Allows to cast a command list manager to other manager types whenever is allowed by the supported engines.
		/// </summary>
		T As();
		EngineType Engines();
		void Set(Rallypoint point);
		void Set(Barrier barrier);
		void Wait(Rallypoint point);
	};

	struct TransferManager : public CommandListManager {
		static EngineType const SupportedEngines = EngineType::TRANSFER;
	private:
		TransferManager();
	};

	struct ComputeManager : public CommandListManager {
		static EngineType const SupportedEngines = (EngineType)((int)EngineType::COMPUTE | (int)EngineType::TRANSFER);
	private:
		ComputeManager();
	};

	struct ComputeExclusiveManager : public CommandListManager {
		static EngineType const SupportedEngines = EngineType::COMPUTE;
	private:
		ComputeExclusiveManager();
	};

	struct GraphicsManager : public CommandListManager {
		static EngineType const SupportedEngines = (EngineType)((int)EngineType::GRAPHICS | (int)EngineType::COMPUTE | (int)EngineType::TRANSFER);

		void Clear(Image2D image, const Formats::R32G32B32A32_SFLOAT &color);

	private:
		GraphicsManager();
	};

	struct RaytracingManager : public CommandListManager {
		static EngineType const SupportedEngines = (EngineType)((int)EngineType::RAYTRACING | (int)EngineType::GRAPHICS | (int)EngineType::COMPUTE | (int)EngineType::TRANSFER);
	private:
		RaytracingManager();
	};


	/// <summary>
	/// Represents the abstraction of a graphic process by means of command list population process.
	/// </summary>
	struct Process {
		virtual EngineType RequiredEngines() = 0;
		virtual void Populate(CommandListManager manager) = 0;
	};

	template<typename I, typename M>
	struct MethodProcess : public Process{
		typedef void(I::* Member)(M);

		Member function;
		I* instance;

		MethodProcess(I* instance, Member function) :instance(instance), function(function) {}

		virtual EngineType RequiredEngines() override;

		virtual void Populate(CommandListManager manager) override;
	};

	struct BufferDescription {
	};

	struct Image1DDescription {
	};

	struct Image2DDescription {
	};

	struct Image3DDescription {
	};

	struct CPUTask : public Obj<states::__CPUTask> {
		void Wait();
	};

	struct GPUTask : public Obj<states::__GPUTask> {
		void Wait();

		static GPUTask Combine(int count, GPUTask* tasks);
	};

	
#define Dispatch_Method(m) Dispatch(this, &decltype(___dr(this))::m)
#define Dispatch_Method_In_Frame_Async(m) Dispatch(this, &decltype(___dr(this))::m, goofy::DispatchMode::ASYNC_FRAME)
#define Dispatch_Method_Async(m) Dispatch(this, &decltype(___dr(this))::m, goofy::DispatchMode::ASYNC)

	/// <summary>
	/// Represents a base class for Presenter and Technique.
	/// </summary>
	class Device : public Obj<states::__Device> {
		friend Presenter;
		friend Technique;
		Device(states::__Device* initialState);

		void BindTechnique(std::shared_ptr<Technique> technique);

	protected:

		std::shared_ptr<BakedProcess> Bake(std::shared_ptr<Process> process);

		CPUTask Dispatch(std::shared_ptr<Process> process, DispatchMode mode = DispatchMode::MAIN_THREAD);

		template<typename I, typename M>
		CPUTask Dispatch(I* instance, typename MethodProcess<I, M>::Member function, DispatchMode mode = DispatchMode::MAIN_THREAD)
		{
			return Dispatch(std::shared_ptr<MethodProcess<I, M>>(new MethodProcess<I, M>(instance, function)), mode);
		}

		template<typename I>
		CPUTask Dispatch(I* instance, typename MethodProcess<I, GraphicsManager>::Member function, DispatchMode mode = DispatchMode::MAIN_THREAD)
		{
			return Dispatch<I, GraphicsManager>(instance, function, mode);
		}

		CPUTask Dispatch(std::shared_ptr<BakedProcess> process, DispatchMode mode = DispatchMode::MAIN_THREAD);

		/// <summary>
		/// Flushes all populating tasks, submit to gpu queues and return a gputask signaling object for further synchronization.
		/// Receives a set of other gpu tasks to wait for on the gpu.
		/// </summary>
		GPUTask Flush(int count, CPUTask* tasks, int waitingCount = 0, GPUTask* waitingGPU = nullptr);

		Buffer Create(const BufferDescription& description);

		Image1D Create(const Image1DDescription& description);

		Image2D Create(const Image2DDescription& description);

		Image3D Create(const Image3DDescription& description);

		Rallypoint CreateRallypoint();

	public:
		/// <summary>
		/// Gets the current frame-in-fly index.
		/// </summary>
		int GetCurrentFrameIndex();

		/// <summary>
		/// Gets the number of frames-in-fly.
		/// </summary>
		int NumberOfFrames();

		/// <summary>
		/// Gets the render target width
		/// </summary>
		int RenderTargetWidth();

		/// <summary>
		/// Gets the render target height
		/// </summary>
		int RenderTargetHeight();

		/// <summary>
		/// Gets the current render target to draw to.
		/// </summary>
		Texture2D GetCurrentRenderTarget();

		/// <summary>
		/// Loads a technique. If argument is null, then a new technique is instanciated.
		/// Additional arguments are discarded if the technique is already instanciated.
		/// Will trigger the OnLoad event of the technique.
		/// </summary>
		template<typename T, typename... A>
		void LoadTechnique(std::shared_ptr<T>& technique, A ...args);

		/// <summary>
		/// Dispatches a technique. This will trigger the OnDispatch event of the technique
		/// </summary>
		template<typename T>
		void DispatchTechnique(const std::shared_ptr<T>& technique);
	};

	class Window : public Obj<states::__Window> {
	public:
		bool IsClosed();

		void PollEvents();

		double Time();

		void* InternalWindow();
	};

	class Presenter : public Device {

		Presenter(const PresenterDescription& description);

	public:
		static void CreateNew(const PresenterDescription& description, std::shared_ptr<Presenter>& presenter);

		void BeginFrame();

		void EndFrame();

		Window Window();
	};

	class Technique : protected Device {
		friend Device;
	protected:
		Technique() : Device(nullptr) { }

		virtual void OnLoad() = 0;

		virtual void OnDispatch() = 0;

	};

	class Resource : public Obj<states::__Resource> {

	};

	class Image2D : public Resource {

	public:
		Texture2D AsTexture(const Sampler& sampler);
	};

	class Texture2D : public Image2D {

	};

	// GENERIC IMPLEMENTATIONS

	template<typename T, typename ...A>
	void Device::LoadTechnique(std::shared_ptr<T>& technique, A ...args)
	{
		if (technique == nullptr)
			technique = std::shared_ptr<T>(new T(args...));

		BindTechnique(technique);

		technique->OnLoad();
	}

	template<typename T>
	void Device::DispatchTechnique(const std::shared_ptr<T>& technique) {
		technique->OnDispatch();
	}

	template<typename T>
	void CommandListManager::CheckCastSupport() {
		if (!(((int)T::SupportedEngines & (int)this->_supported_engines) == (int)T::SupportedEngines))
			throw std::runtime_error("Current command list manager doesnt support the destination engine requirement.");
	}

	template<typename T>
	T CommandListManager::As()
	{
		CheckCastSupport<T>();
		return *(T*)this;
	}

	template<typename I, typename M>
	inline EngineType goofy::MethodProcess<I, M>::RequiredEngines()
	{
		return M::SupportedEngines;
	}

	template<typename I, typename M>
	inline void goofy::MethodProcess<I, M>::Populate(CommandListManager manager)
	{
		(instance->*function)(manager.As<M>());
	}
}

#pragma endregion

#endif