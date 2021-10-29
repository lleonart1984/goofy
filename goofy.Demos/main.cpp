#include "..\goofy.h"

#include <exception>

using namespace goofy;

struct TestTechnique : public Technique {
	// Inherited via Technique
	virtual void OnLoad() override { }

	void Clearing(GraphicsManager manager) {
		manager.Clear(GetCurrentRenderTarget(), Formats::R32G32B32A32_SFLOAT(1, 0, 1, 1));
	}
	
	virtual void OnDispatch() override
	{
		Dispatch_Method(Clearing);
	}
};

void main() {

	try {

		// Create presenter
		std::shared_ptr<Presenter> presenter;
		PresenterDescription description = {};
		description.mode = PresenterCreationMode::NEW_GLFW_WINDOW;
		description.PresentationFormat = Formats::R8G8B8A8::SRGB_Handle();
		description.Usage.RenderTarget = true;
		//description.Usage.Storage = true;
		description.frames = 3;
		description.frame_threads = 0;
		description.async_threads = 0;
		description.resolution.width = 1264;
		description.resolution.height = 761;
		Presenter::CreateNew(description, presenter);

		std::shared_ptr<TestTechnique> testTechnique;

		presenter->LoadTechnique(testTechnique);

		Window window = presenter->Window();

		long current_frame = 0;

		double start_time = window.Time();

		while (!window.IsClosed()) {

			window.PollEvents();

			presenter->BeginFrame();

			presenter->DispatchTechnique(testTechnique);

			presenter->EndFrame();

			current_frame++;

			double mspf = (window.Time() - start_time) / current_frame;

			if (current_frame % 1000 == 0) {
				std::cout << "Time per frame (ms): " << (mspf * 1000) << std::endl;
			}
		}
	}
	catch (std::runtime_error& e) {
		std::cout << e.what() << std::endl;
	}
}