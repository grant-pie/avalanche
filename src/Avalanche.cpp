#include "plugin.hpp"
#include <cmath>
#include <algorithm>

static const int BUFFER_SIZE = 480000; // 10 seconds @ 48kHz
static const int MAX_GRAINS = 64;

struct Grain {
	bool active = false;
	float position = 0.f;      // Sample position in buffer
	float phase = 0.f;         // 0-1 playback progress
	float size = 0.f;          // Grain length in samples
	float pitch = 1.f;         // Playback rate multiplier
	bool reverse = false;      // Playback direction
};

struct Avalanche : Module {
	enum ParamId {
		TIME_PARAM,
		TIME_CV_PARAM,
		SIZE_PARAM,
		SIZE_CV_PARAM,
		DENSITY_PARAM,
		DENSITY_CV_PARAM,
		PITCH_PARAM,
		SPRAY_PARAM,
		SPRAY_CV_PARAM,
		FEEDBACK_PARAM,
		FEEDBACK_CV_PARAM,
		MIX_PARAM,
		FREEZE_PARAM,
		REVERSE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		AUDIO_INPUT,
		TIME_CV_INPUT,
		SIZE_CV_INPUT,
		DENSITY_CV_INPUT,
		PITCH_CV_INPUT,
		SPRAY_CV_INPUT,
		FEEDBACK_CV_INPUT,
		FREEZE_INPUT,
		REVERSE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		AUDIO_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		FREEZE_LIGHT,
		REVERSE_LIGHT,
		LIGHTS_LEN
	};

	float buffer[BUFFER_SIZE] = {};
	int writePos = 0;
	Grain grains[MAX_GRAINS];
	float grainTimer = 0.f;
	float sampleRate = 48000.f;

	// For display
	float displayBuffer[256] = {};
	int displayUpdateCounter = 0;
	int displayWritePos = 0;
	float activeGrainPositions[MAX_GRAINS];
	int activeGrainCount = 0;

	// Schmitt triggers for buttons and gates
	dsp::SchmittTrigger freezeTrigger;
	dsp::SchmittTrigger reverseTrigger;
	bool freezeState = false;
	bool reverseState = false;

	Avalanche() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

		configParam(TIME_PARAM, 0.f, 1.f, 0.5f, "Time", " s", 0.f, 10.f);
		configParam(TIME_CV_PARAM, -1.f, 1.f, 0.f, "Time CV", "%", 0.f, 100.f);
		configParam(SIZE_PARAM, 0.f, 1.f, 0.3f, "Grain Size", " ms", 0.f, 490.f, 10.f);
		configParam(SIZE_CV_PARAM, -1.f, 1.f, 0.f, "Size CV", "%", 0.f, 100.f);
		configParam(DENSITY_PARAM, 0.f, 1.f, 0.3f, "Density", " Hz", 0.f, 49.f, 1.f);
		configParam(DENSITY_CV_PARAM, -1.f, 1.f, 0.f, "Density CV", "%", 0.f, 100.f);
		configParam(PITCH_PARAM, -2.f, 2.f, 0.f, "Pitch", " oct");
		configParam(SPRAY_PARAM, 0.f, 1.f, 0.f, "Spray", "%", 0.f, 100.f);
		configParam(SPRAY_CV_PARAM, -1.f, 1.f, 0.f, "Spray CV", "%", 0.f, 100.f);
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.3f, "Feedback", "%", 0.f, 100.f);
		configParam(FEEDBACK_CV_PARAM, -1.f, 1.f, 0.f, "Feedback CV", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Mix", "%", 0.f, 100.f);
		configButton(FREEZE_PARAM, "Freeze");
		configButton(REVERSE_PARAM, "Reverse");

		configInput(AUDIO_INPUT, "Audio");
		configInput(TIME_CV_INPUT, "Time CV");
		configInput(SIZE_CV_INPUT, "Size CV");
		configInput(DENSITY_CV_INPUT, "Density CV");
		configInput(PITCH_CV_INPUT, "Pitch V/Oct");
		configInput(SPRAY_CV_INPUT, "Spray CV");
		configInput(FEEDBACK_CV_INPUT, "Feedback CV");
		configInput(FREEZE_INPUT, "Freeze Gate");
		configInput(REVERSE_INPUT, "Reverse Gate");

		configOutput(AUDIO_OUTPUT, "Audio");

		configBypass(AUDIO_INPUT, AUDIO_OUTPUT);
	}

	float hannWindow(float phase) {
		return 0.5f * (1.f - std::cos(2.f * M_PI * phase));
	}

	float readBuffer(float position) {
		// Linear interpolation
		int pos0 = (int)position;
		int pos1 = pos0 + 1;
		float frac = position - pos0;

		pos0 = ((pos0 % BUFFER_SIZE) + BUFFER_SIZE) % BUFFER_SIZE;
		pos1 = ((pos1 % BUFFER_SIZE) + BUFFER_SIZE) % BUFFER_SIZE;

		return buffer[pos0] * (1.f - frac) + buffer[pos1] * frac;
	}

	void triggerGrain(float position, float size, float pitch, bool reverse) {
		// Find inactive grain slot
		for (int i = 0; i < MAX_GRAINS; i++) {
			if (!grains[i].active) {
				grains[i].active = true;
				grains[i].position = position;
				grains[i].phase = 0.f;
				grains[i].size = size;
				grains[i].pitch = pitch;
				grains[i].reverse = reverse;
				return;
			}
		}
	}

	void process(const ProcessArgs& args) override {
		sampleRate = args.sampleRate;
		float input = inputs[AUDIO_INPUT].getVoltage() / 5.f;

		// Handle freeze button and gate
		if (freezeTrigger.process(params[FREEZE_PARAM].getValue() > 0.f)) {
			freezeState = !freezeState;
		}
		bool freeze = freezeState || (inputs[FREEZE_INPUT].getVoltage() > 1.f);
		lights[FREEZE_LIGHT].setBrightness(freeze ? 1.f : 0.f);

		// Handle reverse button and gate
		if (reverseTrigger.process(params[REVERSE_PARAM].getValue() > 0.f)) {
			reverseState = !reverseState;
		}
		bool reverse = reverseState || (inputs[REVERSE_INPUT].getVoltage() > 1.f);
		lights[REVERSE_LIGHT].setBrightness(reverse ? 1.f : 0.f);

		// Get parameters with CV
		float time = params[TIME_PARAM].getValue();
		if (inputs[TIME_CV_INPUT].isConnected()) {
			time += inputs[TIME_CV_INPUT].getVoltage() / 10.f * params[TIME_CV_PARAM].getValue();
		}
		time = clamp(time, 0.f, 1.f);
		float delaySamples = time * (BUFFER_SIZE - 1);

		float sizeParam = params[SIZE_PARAM].getValue();
		if (inputs[SIZE_CV_INPUT].isConnected()) {
			sizeParam += inputs[SIZE_CV_INPUT].getVoltage() / 10.f * params[SIZE_CV_PARAM].getValue();
		}
		sizeParam = clamp(sizeParam, 0.f, 1.f);
		float grainSize = (10.f + sizeParam * 490.f) * sampleRate / 1000.f; // 10-500ms in samples

		float densityParam = params[DENSITY_PARAM].getValue();
		if (inputs[DENSITY_CV_INPUT].isConnected()) {
			densityParam += inputs[DENSITY_CV_INPUT].getVoltage() / 10.f * params[DENSITY_CV_PARAM].getValue();
		}
		densityParam = clamp(densityParam, 0.f, 1.f);
		float density = 1.f + densityParam * 49.f; // 1-50 Hz

		float pitchParam = params[PITCH_PARAM].getValue();
		if (inputs[PITCH_CV_INPUT].isConnected()) {
			pitchParam += inputs[PITCH_CV_INPUT].getVoltage();
		}
		float pitch = std::pow(2.f, pitchParam);

		float sprayParam = params[SPRAY_PARAM].getValue();
		if (inputs[SPRAY_CV_INPUT].isConnected()) {
			sprayParam += inputs[SPRAY_CV_INPUT].getVoltage() / 10.f * params[SPRAY_CV_PARAM].getValue();
		}
		sprayParam = clamp(sprayParam, 0.f, 1.f);
		float spray = sprayParam * sampleRate; // Up to 1 second of random offset

		float feedbackParam = params[FEEDBACK_PARAM].getValue();
		if (inputs[FEEDBACK_CV_INPUT].isConnected()) {
			feedbackParam += inputs[FEEDBACK_CV_INPUT].getVoltage() / 10.f * params[FEEDBACK_CV_PARAM].getValue();
		}
		feedbackParam = clamp(feedbackParam, 0.f, 0.99f);

		float mix = params[MIX_PARAM].getValue();

		// Grain triggering
		grainTimer += args.sampleTime;
		float grainInterval = 1.f / density;
		if (grainTimer >= grainInterval) {
			grainTimer -= grainInterval;

			// Calculate grain start position with spray
			float grainPos = writePos - delaySamples;
			if (spray > 0.f) {
				grainPos += (random::uniform() * 2.f - 1.f) * spray;
			}

			triggerGrain(grainPos, grainSize, pitch, reverse);
		}

		// Process active grains
		float grainOutput = 0.f;
		activeGrainCount = 0;

		for (int i = 0; i < MAX_GRAINS; i++) {
			if (grains[i].active) {
				// Calculate envelope
				float envelope = hannWindow(grains[i].phase);

				// Calculate read position
				float readPos;
				if (grains[i].reverse) {
					readPos = grains[i].position + (1.f - grains[i].phase) * grains[i].size;
				} else {
					readPos = grains[i].position + grains[i].phase * grains[i].size;
				}

				// Read sample with interpolation
				float sample = readBuffer(readPos);
				grainOutput += sample * envelope;

				// Advance grain phase based on pitch
				grains[i].phase += grains[i].pitch / grains[i].size;

				// Store position for display
				if (activeGrainCount < MAX_GRAINS) {
					activeGrainPositions[activeGrainCount++] = readPos;
				}

				// Deactivate finished grains
				if (grains[i].phase >= 1.f) {
					grains[i].active = false;
				}
			}
		}

		// Write to buffer (with feedback)
		if (!freeze) {
			buffer[writePos] = input + grainOutput * feedbackParam;
			writePos = (writePos + 1) % BUFFER_SIZE;
		}

		// Mix dry and wet
		float output = input * (1.f - mix) + grainOutput * mix;
		outputs[AUDIO_OUTPUT].setVoltage(output * 5.f);

		// Update display buffer periodically
		displayUpdateCounter++;
		if (displayUpdateCounter >= (int)(sampleRate / 60.f)) { // ~60 Hz update
			displayUpdateCounter = 0;
			displayWritePos = writePos;
			int samplesPerPixel = BUFFER_SIZE / 256;
			for (int i = 0; i < 256; i++) {
				float maxVal = 0.f;
				int startIdx = i * samplesPerPixel;
				for (int j = 0; j < samplesPerPixel; j += 16) {
					int idx = (startIdx + j) % BUFFER_SIZE;
					maxVal = std::max(maxVal, std::abs(buffer[idx]));
				}
				displayBuffer[i] = std::min(maxVal, 1.f);
			}
		}
	}

	json_t* dataToJson() override {
		json_t* rootJ = json_object();
		json_object_set_new(rootJ, "freezeState", json_boolean(freezeState));
		json_object_set_new(rootJ, "reverseState", json_boolean(reverseState));
		return rootJ;
	}

	void dataFromJson(json_t* rootJ) override {
		json_t* freezeJ = json_object_get(rootJ, "freezeState");
		if (freezeJ) freezeState = json_boolean_value(freezeJ);
		json_t* reverseJ = json_object_get(rootJ, "reverseState");
		if (reverseJ) reverseState = json_boolean_value(reverseJ);
	}
};

struct BufferDisplay : LightWidget {
	Avalanche* module;

	BufferDisplay() {
		// Size is set by the parent widget
	}

	void draw(const DrawArgs& args) override {
		nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);

		// Background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(20, 20, 25));
		nvgFill(args.vg);

		if (!module) {
			// Preview mode - draw placeholder
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0, box.size.y / 2);
			for (int i = 0; i < (int)box.size.x; i++) {
				float y = box.size.y / 2 + std::sin(i * 0.1f) * 10.f;
				nvgLineTo(args.vg, i, y);
			}
			nvgStrokeColor(args.vg, nvgRGBA(100, 200, 255, 150));
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
			return;
		}

		// Draw waveform
		nvgBeginPath(args.vg);
		float maxH = box.size.y * 0.9f;
		for (int i = 0; i < 256; i++) {
			float x = i * box.size.x / 256.f;
			float h = std::min(module->displayBuffer[i] * box.size.y * 0.9f, maxH);
			nvgRect(args.vg, x, (box.size.y - h) / 2, box.size.x / 256.f, h);
		}
		nvgFillColor(args.vg, nvgRGBA(60, 150, 200, 200));
		nvgFill(args.vg);

		// Draw write position (snapshotted at ~60Hz to avoid flicker)
		float writeX = (float)module->displayWritePos / BUFFER_SIZE * box.size.x;
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, writeX, 0);
		nvgLineTo(args.vg, writeX, box.size.y);
		nvgStrokeColor(args.vg, nvgRGBA(255, 100, 100, 200));
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);

		// Draw active grain positions
		nvgFillColor(args.vg, nvgRGBA(255, 220, 100, 180));
		for (int i = 0; i < module->activeGrainCount; i++) {
			float pos = module->activeGrainPositions[i];
			pos = std::fmod(pos + BUFFER_SIZE, (float)BUFFER_SIZE);
			float x = pos / BUFFER_SIZE * box.size.x;
			nvgBeginPath(args.vg);
			nvgCircle(args.vg, x, box.size.y / 2, 3.f);
			nvgFill(args.vg);
		}

		// Border
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgStrokeColor(args.vg, nvgRGB(80, 80, 90));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		nvgResetScissor(args.vg);
	}
};

struct PanelLabel : Widget {
	std::string text;
	NVGcolor color;
	float fontSize;
	int align;

	PanelLabel(Vec pos, std::string text, float fontSize = 11.f,
	           NVGcolor color = nvgRGB(0xc0, 0xc0, 0xd0),
	           int align = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE)
		: text(text), color(color), fontSize(fontSize), align(align) {
		box.pos = pos;
		box.size = Vec(0, 0);
	}

	void draw(const DrawArgs& args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font)
			return;
		nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, fontSize);
		nvgTextAlign(args.vg, align);
		nvgFillColor(args.vg, color);
		nvgText(args.vg, 0, 0, text.c_str(), NULL);
	}
};

struct AvalanchePanel : Widget {
	AvalanchePanel(Vec size) {
		box.size = size;
	}

	void draw(const DrawArgs& args) override {
		auto mpx = [](float mm) { return mm * 75.f / 25.4f; };

		// Background
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(args.vg, nvgRGB(0x2a, 0x2a, 0x2a));
		nvgFill(args.vg);

		// Inner panel face
		float inset = 4.f;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, inset, inset, box.size.x - 2.f * inset, box.size.y - 2.f * inset, 5.f);
		nvgFillColor(args.vg, nvgRGB(0x22, 0x22, 0x22));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x33, 0x33, 0x33));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Display area background + border
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, mpx(8.f), mpx(11.9f), mpx(105.92f), mpx(20.f), mpx(2.f));
		nvgFillColor(args.vg, nvgRGB(0x15, 0x15, 0x18));
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, nvgRGB(0x50, 0x50, 0x60));
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);

		// Separator lines
		auto sep = [&](float ymm) {
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, mpx(5.f), mpx(ymm));
			nvgLineTo(args.vg, mpx(116.9f), mpx(ymm));
			nvgStrokeColor(args.vg, nvgRGB(0x44, 0x44, 0x44));
			nvgStrokeWidth(args.vg, 0.8f);
			nvgStroke(args.vg);
		};
		sep(33.9f);  // Below display
		sep(81.9f);  // Above CV inputs
		sep(101.9f); // Above audio I/O

		// Font
		std::shared_ptr<Font> font = APP->window->loadFont(
			asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font) return;
		nvgFontFaceId(args.vg, font->handle);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		float cx = box.size.x / 2.f;

		// Title — centred between panel top and visualizer top
		nvgFontSize(args.vg, 11.f);
		nvgFillColor(args.vg, nvgRGB(0xff, 0xff, 0xff));
		nvgText(args.vg, cx, 17.5f, "AVALANCHE", NULL);

		// Row 1 parameter labels
		nvgFontSize(args.vg, 8.5f);
		nvgFillColor(args.vg, nvgRGB(0x88, 0x88, 0x88));
		nvgText(args.vg, mpx(20.0f),  mpx(37.7f), "TIME",    NULL);
		nvgText(args.vg, mpx(48.1f),  mpx(37.7f), "SIZE",    NULL);
		nvgText(args.vg, mpx(76.2f),  mpx(37.7f), "DENSITY", NULL);
		nvgText(args.vg, mpx(104.3f), mpx(37.7f), "PITCH",   NULL);

		// Row 2 parameter labels
		nvgText(args.vg, mpx(20.0f), mpx(54.2f), "SPRAY",    NULL);
		nvgText(args.vg, mpx(48.1f), mpx(54.2f), "FEEDBACK", NULL);
		nvgText(args.vg, mpx(76.2f), mpx(54.2f), "MIX",      NULL);

		// Button labels — same 8mm gap as param labels to knobs
		nvgText(args.vg, mpx(39.9f), mpx(70.2f), "FREEZE",  NULL);
		nvgText(args.vg, mpx(82.2f), mpx(70.2f), "REVERSE", NULL);

		// CV input labels
		nvgFontSize(args.vg, 7.f);
		nvgText(args.vg,  54.f, mpx(85.9f), "TIME",  NULL);
		nvgText(args.vg,  90.f, mpx(85.9f), "SIZE",  NULL);
		nvgText(args.vg, 126.f, mpx(85.9f), "DENS",  NULL);
		nvgText(args.vg, 162.f, mpx(85.9f), "V/OCT", NULL);
		nvgText(args.vg, 198.f, mpx(85.9f), "SPRY",  NULL);
		nvgText(args.vg, 234.f, mpx(85.9f), "FDBK",  NULL);
		nvgText(args.vg, 270.f, mpx(85.9f), "FRZ",   NULL);
		nvgText(args.vg, 306.f, mpx(85.9f), "REV",   NULL);

		// Audio I/O labels
		nvgFontSize(args.vg, 8.5f);
		nvgFillColor(args.vg, nvgRGB(0xcc, 0xcc, 0xcc));
		nvgText(args.vg, mpx(20.0f),  mpx(105.5f), "IN",  NULL);
		nvgText(args.vg, 302.f, mpx(105.5f), "OUT", NULL);

		// VONK — "VON" near-white, "K" accent purple, combined word centred
		nvgFontSize(args.vg, 7.f);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		float vkBounds[4];
		nvgTextBounds(args.vg, 0, 0, "VON",  NULL, vkBounds);
		float vonWidth = vkBounds[2] - vkBounds[0];
		nvgTextBounds(args.vg, 0, 0, "VONK", NULL, vkBounds);
		float vonkWidth = vkBounds[2] - vkBounds[0];
		float vkX = cx - vonkWidth / 2.f;
		float vkY = mpx(115.2f);
		nvgFillColor(args.vg, nvgRGB(0xf4, 0xf5, 0xf7));
		nvgText(args.vg, vkX,            vkY, "VON", NULL);
		nvgFillColor(args.vg, nvgRGB(0xc0, 0x84, 0xfc));
		nvgText(args.vg, vkX + vonWidth, vkY, "K",   NULL);
	}
};

struct AvalancheWidget : ModuleWidget {
	AvalancheWidget(Avalanche* module) {
		setModule(module);
		box.size = Vec(RACK_GRID_WIDTH * 24, RACK_GRID_HEIGHT);
		addChild(new AvalanchePanel(box.size));

		// Screws
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		// Display
		BufferDisplay* display = new BufferDisplay();
		display->box.pos = Vec(24, 35);
		display->box.size = Vec(313, 59);
		display->module = module;
		addChild(display);

		// Row 1: Time, Size, Density, Pitch
		float row1Y = 135;
		addParam(createParamCentered<RoundBlackKnob>(Vec(59,  row1Y), module, Avalanche::TIME_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(142, row1Y), module, Avalanche::SIZE_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(225, row1Y), module, Avalanche::DENSITY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(308, row1Y), module, Avalanche::PITCH_PARAM));

		// Row 2: Spray, Feedback, Mix
		float row2Y = 184;
		addParam(createParamCentered<RoundBlackKnob>(Vec(59,  row2Y), module, Avalanche::SPRAY_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(142, row2Y), module, Avalanche::FEEDBACK_PARAM));
		addParam(createParamCentered<RoundBlackKnob>(Vec(225, row2Y), module, Avalanche::MIX_PARAM));

		// CV Attenuverters next to their knobs
		addParam(createParamCentered<Trimpot>(Vec(95,  row1Y), module, Avalanche::TIME_CV_PARAM));
		addParam(createParamCentered<Trimpot>(Vec(178, row1Y), module, Avalanche::SIZE_CV_PARAM));
		addParam(createParamCentered<Trimpot>(Vec(261, row1Y), module, Avalanche::DENSITY_CV_PARAM));
		addParam(createParamCentered<Trimpot>(Vec(95,  row2Y), module, Avalanche::SPRAY_CV_PARAM));
		addParam(createParamCentered<Trimpot>(Vec(178, row2Y), module, Avalanche::FEEDBACK_CV_PARAM));

		// Buttons: Freeze, Reverse
		float row3Y = 226;
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(Vec(118, row3Y), module, Avalanche::FREEZE_PARAM, Avalanche::FREEZE_LIGHT));
		addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(Vec(243, row3Y), module, Avalanche::REVERSE_PARAM, Avalanche::REVERSE_LIGHT));

		// CV Inputs row
		float cvY = 272;
		addInput(createInputCentered<PJ301MPort>(Vec(54,  cvY), module, Avalanche::TIME_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(90,  cvY), module, Avalanche::SIZE_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(126, cvY), module, Avalanche::DENSITY_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(162, cvY), module, Avalanche::PITCH_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(198, cvY), module, Avalanche::SPRAY_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(234, cvY), module, Avalanche::FEEDBACK_CV_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(270, cvY), module, Avalanche::FREEZE_INPUT));
		addInput(createInputCentered<PJ301MPort>(Vec(306, cvY), module, Avalanche::REVERSE_INPUT));

		// Audio I/O
		float ioY = 330;
		addInput(createInputCentered<PJ301MPort>(Vec(59,  ioY), module, Avalanche::AUDIO_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(Vec(302, ioY), module, Avalanche::AUDIO_OUTPUT));

	}
};

Model* modelAvalanche = createModel<Avalanche, AvalancheWidget>("Avalanche");
