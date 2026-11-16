#include <array>
#include <cmath>
#include <vector>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

namespace
{
constexpr auto parameterTreeId = "VocalSuiteParameters";
constexpr auto inputGainId = "inputGain";
constexpr auto outputGainId = "outputGain";
constexpr auto mixId = "mix";
constexpr auto driveId = "drive";
constexpr auto deEssId = "deEss";
constexpr auto lowShelfId = "lowShelf";
constexpr auto presenceId = "presence";
constexpr auto airId = "air";
constexpr auto compressorId = "compressor";
constexpr auto limiterCeilingId = "limiterCeiling";
constexpr auto stereoWidthId = "stereoWidth";
constexpr auto bypassId = "bypass";
}

class VocalSuiteAudioProcessor : public juce::AudioProcessor
{
public:
    VocalSuiteAudioProcessor()
        : AudioProcessor(BusesProperties()
              .withInput("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, parameterTreeId, createParameterLayout())
    {
        for (auto& filter : lowShelfFilters_)
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(44100.0f, 120.0f, 0.0f, 0.75f);
        for (auto& filter : presenceFilters_)
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(44100.0f, 2500.0f, 0.0f, 0.9f);
        for (auto& filter : airFilters_)
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(44100.0f, 10000.0f, 0.0f, 0.75f);
    }

    ~VocalSuiteAudioProcessor() override = default;

    const juce::String getName() const override { return "VocalSuite"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        juce::ignoreUnused(samplesPerBlock);
        sampleRate_ = sampleRate;
        inputGainSmoothed_.reset(sampleRate, 0.05);
        outputGainSmoothed_.reset(sampleRate, 0.05);
        mixSmoothed_.reset(sampleRate, 0.05);
        driveSmoothed_.reset(sampleRate, 0.05);
        deEssSmoothed_.reset(sampleRate, 0.05);
        compressorSmoothed_.reset(sampleRate, 0.04);
        limiterSmoothed_.reset(sampleRate, 0.02);
        widthSmoothed_.reset(sampleRate, 0.05);
        updateFilterCoefficients();
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
            && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        processAudioBuffer<float>(buffer);
    }

    void processBlock(juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        processAudioBuffer<double>(buffer);
    }

    juce::AudioProcessorEditor* createEditor() override
    {
        return new VocalSuiteAudioProcessorEditor(*this);
    }

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        auto state = apvts.copyState();
        std::unique_ptr<juce::XmlElement> xml(state.createXml());
        copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override
    {
        std::unique_ptr<juce::XmlElement> xml = getXmlFromBinary(data, static_cast<size_t>(sizeInBytes));
        if (xml == nullptr)
            return;

        if (auto tree = juce::ValueTree::fromXml(*xml); tree.isValid())
            apvts.replaceState(tree);
    }

    bool hasEditor() const override { return true; }
    bool supportsDoublePrecisionProcessing() const override { return true; }

    static juce::AudioProcessor* createPluginFilter() { return new VocalSuiteAudioProcessor(); }

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        return {
            std::make_unique<juce::AudioParameterFloat>(inputGainId, "Input Gain", -12.0f, 12.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(outputGainId, "Output Gain", -12.0f, 12.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(mixId, "Mix", 0.0f, 1.0f, 0.75f),
            std::make_unique<juce::AudioParameterFloat>(driveId, "Drive", 0.0f, 1.0f, 0.25f),
            std::make_unique<juce::AudioParameterFloat>(deEssId, "De-ess", 0.0f, 1.0f, 0.2f),
            std::make_unique<juce::AudioParameterFloat>(lowShelfId, "Low Shelf", -12.0f, 12.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(presenceId, "Presence", -12.0f, 12.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(airId, "Air", -12.0f, 12.0f, 0.0f),
            std::make_unique<juce::AudioParameterFloat>(compressorId, "Compression", 0.0f, 1.0f, 0.35f),
            std::make_unique<juce::AudioParameterFloat>(limiterCeilingId, "Limiter", -6.0f, 0.0f, -1.0f),
            std::make_unique<juce::AudioParameterFloat>(stereoWidthId, "Stereo Width", 0.0f, 2.0f, 1.0f),
            std::make_unique<juce::AudioParameterBool>(bypassId, "Bypass", false)
        };
    }

    void applyPreset(const juce::String& presetName)
    {
        auto setParam = [this](const juce::String& id, float value) {
            apvts.getParameterAsValue(id) = value;
        };

        if (presetName == "Neutral")
        {
            setParam(inputGainId, 0.0f);
            setParam(outputGainId, 0.0f);
            setParam(mixId, 0.75f);
            setParam(driveId, 0.25f);
            setParam(deEssId, 0.2f);
            setParam(lowShelfId, 0.0f);
            setParam(presenceId, 0.0f);
            setParam(airId, 0.0f);
            setParam(compressorId, 0.35f);
            setParam(limiterCeilingId, -1.0f);
            setParam(stereoWidthId, 1.0f);
            setParam(bypassId, 0.0f);
        }
        else if (presetName == "Warm")
        {
            setParam(inputGainId, 1.5f);
            setParam(outputGainId, 0.0f);
            setParam(mixId, 0.8f);
            setParam(driveId, 0.35f);
            setParam(deEssId, 0.25f);
            setParam(lowShelfId, 3.5f);
            setParam(presenceId, 1.0f);
            setParam(airId, -1.5f);
            setParam(compressorId, 0.45f);
            setParam(limiterCeilingId, -1.5f);
            setParam(stereoWidthId, 1.2f);
            setParam(bypassId, 0.0f);
        }
        else if (presetName == "Bright")
        {
            setParam(inputGainId, 0.0f);
            setParam(outputGainId, 1.0f);
            setParam(mixId, 0.7f);
            setParam(driveId, 0.5f);
            setParam(deEssId, 0.15f);
            setParam(lowShelfId, 0.0f);
            setParam(presenceId, 2.5f);
            setParam(airId, 3.0f);
            setParam(compressorId, 0.3f);
            setParam(limiterCeilingId, -2.0f);
            setParam(stereoWidthId, 1.35f);
            setParam(bypassId, 0.0f);
        }
        else
        {
            applyPreset("Neutral");
        }
    }

private:
    template <typename FloatType>
    void processAudioBuffer(juce::AudioBuffer<FloatType>& buffer)
    {
        const auto numChannels = buffer.getNumChannels();
        const auto numSamples = buffer.getNumSamples();
        if (numChannels == 0 || numSamples == 0)
            return;

        updateFilterCoefficients();

        const auto inputGainTarget = static_cast<FloatType>(apvts.getRawParameterValue(inputGainId)->load());
        const auto outputGainTarget = static_cast<FloatType>(apvts.getRawParameterValue(outputGainId)->load());
        const auto mixTarget = static_cast<FloatType>(apvts.getRawParameterValue(mixId)->load());
        const auto driveTarget = static_cast<FloatType>(apvts.getRawParameterValue(driveId)->load());
        const auto deEssTarget = static_cast<FloatType>(apvts.getRawParameterValue(deEssId)->load());
        const auto compressorTarget = static_cast<FloatType>(apvts.getRawParameterValue(compressorId)->load());
        const auto limiterCeilingTarget = static_cast<FloatType>(apvts.getRawParameterValue(limiterCeilingId)->load());
        const auto stereoWidthTarget = static_cast<FloatType>(apvts.getRawParameterValue(stereoWidthId)->load());
        const auto bypass = apvts.getRawParameterValue(bypassId)->load() > 0.5;

        if (bypass)
            return;

        inputGainSmoothed_.setTargetValue(static_cast<float>(inputGainTarget));
        outputGainSmoothed_.setTargetValue(static_cast<float>(outputGainTarget));
        mixSmoothed_.setTargetValue(static_cast<float>(mixTarget));
        driveSmoothed_.setTargetValue(static_cast<float>(driveTarget));
        deEssSmoothed_.setTargetValue(static_cast<float>(deEssTarget));
        compressorSmoothed_.setTargetValue(static_cast<float>(compressorTarget));
        limiterSmoothed_.setTargetValue(static_cast<float>(limiterCeilingTarget));
        widthSmoothed_.setTargetValue(static_cast<float>(stereoWidthTarget));

        const auto filterIndex = [](int channel, std::size_t bankSize) -> std::size_t {
            return bankSize == 0 ? 0u : static_cast<std::size_t>(channel) % bankSize;
        };

        for (int sample = 0; sample < numSamples; ++sample)
        {
            const auto inputGain = static_cast<FloatType>(inputGainSmoothed_.getNextValue());
            const auto outputGain = static_cast<FloatType>(outputGainSmoothed_.getNextValue());
            const auto mix = static_cast<FloatType>(mixSmoothed_.getNextValue());
            const auto drive = static_cast<FloatType>(driveSmoothed_.getNextValue());
            const auto deEss = static_cast<FloatType>(deEssSmoothed_.getNextValue());
            const auto compressor = static_cast<FloatType>(compressorSmoothed_.getNextValue());
            const auto limiterCeiling = static_cast<FloatType>(limiterSmoothed_.getNextValue());
            const auto widthFactor = juce::jlimit(static_cast<FloatType>(0.0), static_cast<FloatType>(2.0), static_cast<FloatType>(widthSmoothed_.getNextValue()));
            const auto inputGainDb = static_cast<FloatType>(juce::Decibels::decibelsToGain(static_cast<float>(inputGain)));
            const auto outputGainDb = static_cast<FloatType>(juce::Decibels::decibelsToGain(static_cast<float>(outputGain)));
            const auto limiterLimit = static_cast<FloatType>(juce::Decibels::decibelsToGain(static_cast<float>(limiterCeiling)));

            auto left = static_cast<FloatType>(buffer.getWritePointer(0)[sample]);
            auto right = numChannels > 1 ? static_cast<FloatType>(buffer.getWritePointer(1)[sample]) : left;

            const auto processChannel = [&](FloatType& sampleValue, int channelIndex) {
                const auto original = sampleValue;

                sampleValue *= inputGainDb;
                sampleValue = lowShelfFilters_[filterIndex(channelIndex, lowShelfFilters_.size())].processSample(sampleValue);
                sampleValue = presenceFilters_[filterIndex(channelIndex, presenceFilters_.size())].processSample(sampleValue);
                sampleValue = airFilters_[filterIndex(channelIndex, airFilters_.size())].processSample(sampleValue);

                const auto driveGain = static_cast<FloatType>(1.0) + drive * static_cast<FloatType>(2.5);
                const auto clipped = std::tanh(sampleValue * driveGain);
                sampleValue = clipped;

                const auto sibilanceLevel = std::abs(sampleValue);
                const auto deEssAmount = deEss * static_cast<FloatType>(0.75);
                if (sibilanceLevel > static_cast<FloatType>(0.2))
                {
                    const auto reduction = (sibilanceLevel - static_cast<FloatType>(0.2)) / static_cast<FloatType>(0.8);
                    sampleValue *= static_cast<FloatType>(1.0) - deEssAmount * reduction;
                }

                const auto envelope = std::abs(sampleValue);
                const auto threshold = static_cast<FloatType>(0.55);
                if (envelope > threshold)
                {
                    const auto reduction = juce::jlimit(static_cast<FloatType>(0.0), static_cast<FloatType>(1.0),
                        (envelope - threshold) / static_cast<FloatType>(0.45));
                    sampleValue *= static_cast<FloatType>(1.0) - compressor * reduction * static_cast<FloatType>(0.8);
                }

                const auto dry = original;
                const auto wet = sampleValue;
                sampleValue = dry * (static_cast<FloatType>(1.0) - mix) + wet * mix;
            };

            processChannel(left, 0);
            if (numChannels > 1)
                processChannel(right, 1);

            if (numChannels > 1)
            {
                const auto mid = (left + right) * static_cast<FloatType>(0.5);
                const auto side = (left - right) * static_cast<FloatType>(0.5);
                const auto widenedLeft = mid + side * widthFactor;
                const auto widenedRight = mid - side * widthFactor;
                left = widenedLeft;
                right = widenedRight;
            }

            left *= outputGainDb;
            right *= outputGainDb;

            const auto limitSample = [](FloatType value, FloatType limit) {
                if (std::abs(value) > limit)
                    return value > static_cast<FloatType>(0.0) ? limit : -limit;
                return value;
            };

            left = limitSample(left, limiterLimit);
            right = limitSample(right, limiterLimit);

            left = juce::jlimit(static_cast<FloatType>(-1.0), static_cast<FloatType>(1.0), left);
            right = juce::jlimit(static_cast<FloatType>(-1.0), static_cast<FloatType>(1.0), right);

            buffer.getWritePointer(0)[sample] = left;
            if (numChannels > 1)
                buffer.getWritePointer(1)[sample] = right;
        }
    }

    void updateFilterCoefficients()
    {
        if (sampleRate_ <= 0.0)
            return;

        const auto lowShelfGain = apvts.getRawParameterValue(lowShelfId)->load();
        const auto presenceGain = apvts.getRawParameterValue(presenceId)->load();
        const auto airGain = apvts.getRawParameterValue(airId)->load();

        for (auto& filter : lowShelfFilters_)
        {
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowShelf(sampleRate_, 120.0f, static_cast<float>(lowShelfGain), 0.75f);
            filter.reset();
        }

        for (auto& filter : presenceFilters_)
        {
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(sampleRate_, 2500.0f, static_cast<float>(presenceGain), 0.9f);
            filter.reset();
        }

        for (auto& filter : airFilters_)
        {
            filter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(sampleRate_, 10000.0f, static_cast<float>(airGain), 0.75f);
            filter.reset();
        }
    }

    double sampleRate_ = 44100.0;
    juce::AudioProcessorValueTreeState apvts;
    std::array<juce::dsp::IIR::Filter<float>, 2> lowShelfFilters_;
    std::array<juce::dsp::IIR::Filter<float>, 2> presenceFilters_;
    std::array<juce::dsp::IIR::Filter<float>, 2> airFilters_;
    juce::SmoothedValue<float> inputGainSmoothed_;
    juce::SmoothedValue<float> outputGainSmoothed_;
    juce::SmoothedValue<float> mixSmoothed_;
    juce::SmoothedValue<float> driveSmoothed_;
    juce::SmoothedValue<float> deEssSmoothed_;
    juce::SmoothedValue<float> compressorSmoothed_;
    juce::SmoothedValue<float> limiterSmoothed_;
    juce::SmoothedValue<float> widthSmoothed_;

public:
    class VocalSuiteAudioProcessorEditor : public juce::AudioProcessorEditor
    {
    public:
        explicit VocalSuiteAudioProcessorEditor(VocalSuiteAudioProcessor& p)
            : juce::AudioProcessorEditor(&p), processor(p)
        {
            setSize(760, 320);
            addAndMakeVisible(titleLabel_);
            titleLabel_.setText("VocalSuite", juce::dontSendNotification);
            titleLabel_.setJustificationType(juce::Justification::centredLeft);
            titleLabel_.setFont(juce::Font(juce::FontOptions { 28.0f }));

            addAndMakeVisible(bypassButton_);
            bypassButton_.setButtonText("Bypass");

            addAndMakeVisible(neutralPresetButton_);
            neutralPresetButton_.setButtonText("Neutral");
            neutralPresetButton_.onClick = [this] { processor.applyPreset("Neutral"); };

            addAndMakeVisible(warmPresetButton_);
            warmPresetButton_.setButtonText("Warm");
            warmPresetButton_.onClick = [this] { processor.applyPreset("Warm"); };

            addAndMakeVisible(brightPresetButton_);
            brightPresetButton_.setButtonText("Bright");
            brightPresetButton_.onClick = [this] { processor.applyPreset("Bright"); };

            setupSlider(inputGainSlider_, "Input", -12.0f, 12.0f, 0.1f);
            setupSlider(outputGainSlider_, "Output", -12.0f, 12.0f, 0.1f);
            setupSlider(mixSlider_, "Mix", 0.0f, 1.0f, 0.01f);
            setupSlider(driveSlider_, "Drive", 0.0f, 1.0f, 0.01f);
            setupSlider(deEssSlider_, "De-ess", 0.0f, 1.0f, 0.01f);
            setupSlider(lowShelfSlider_, "Low Shelf", -12.0f, 12.0f, 0.1f);
            setupSlider(presenceSlider_, "Presence", -12.0f, 12.0f, 0.1f);
            setupSlider(airSlider_, "Air", -12.0f, 12.0f, 0.1f);
            setupSlider(compressorSlider_, "Compression", 0.0f, 1.0f, 0.01f);
            setupSlider(limiterSlider_, "Limiter", -6.0f, 0.0f, 0.1f);
            setupSlider(widthSlider_, "Stereo Width", 0.0f, 2.0f, 0.01f);

            inputGainAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), inputGainId, inputGainSlider_);
            outputGainAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), outputGainId, outputGainSlider_);
            mixAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), mixId, mixSlider_);
            driveAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), driveId, driveSlider_);
            deEssAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), deEssId, deEssSlider_);
            lowShelfAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), lowShelfId, lowShelfSlider_);
            presenceAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), presenceId, presenceSlider_);
            airAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), airId, airSlider_);
            compressorAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), compressorId, compressorSlider_);
            limiterAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), limiterCeilingId, limiterSlider_);
            widthAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.getAPVTS(), stereoWidthId, widthSlider_);
            bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.getAPVTS(), bypassId, bypassButton_);
        }

        ~VocalSuiteAudioProcessorEditor() override = default;

        void paint(juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            g.fillAll(juce::Colours::black);
            g.setColour(juce::Colour(0xff2f6bff));
            g.fillRoundedRectangle(bounds.reduced(10.0f), 14.0f);
            g.setColour(juce::Colours::white);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(16);
            titleLabel_.setBounds(bounds.removeFromTop(38));

            auto presetRow = bounds.removeFromTop(28);
            neutralPresetButton_.setBounds(presetRow.removeFromLeft(90));
            warmPresetButton_.setBounds(presetRow.removeFromLeft(90));
            brightPresetButton_.setBounds(presetRow.removeFromLeft(90));
            bypassButton_.setBounds(presetRow.withTrimmedLeft(10).withTrimmedRight(10));

            auto sliderBounds = bounds;
            const auto columnWidth = sliderBounds.getWidth() / 3;
            auto left = sliderBounds.removeFromLeft(columnWidth);
            auto middle = sliderBounds.removeFromLeft(columnWidth);
            auto right = sliderBounds;

            layoutColumn(left, { &inputGainSlider_, &outputGainSlider_, &mixSlider_ });
            layoutColumn(middle, { &driveSlider_, &deEssSlider_, &lowShelfSlider_ });
            layoutColumn(right, { &presenceSlider_, &airSlider_, &compressorSlider_ });
            auto bottom = getLocalBounds().reduced(16).removeFromBottom(60);
            layoutColumn(bottom, { &limiterSlider_, &widthSlider_ });
        }

    private:
        void setupSlider(juce::Slider& slider, const juce::String& name, float min, float max, float step)
        {
            slider.setName(name);
            slider.setRange(min, max, step);
            slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 68, 20);
            slider.setPopupDisplayEnabled(true, true, this);
            addAndMakeVisible(slider);

            auto label = std::make_unique<juce::Label>();
            label->setName(name);
            label->setText(name, juce::dontSendNotification);
            label->setJustificationType(juce::Justification::centredLeft);
            label->setFont(juce::Font(juce::FontOptions{}.withPointHeight(13.0f)));
            labels_.push_back(std::move(label));
            addAndMakeVisible(*labels_.back());
        }

        juce::Label* findLabelForSlider(const juce::Slider& slider)
        {
            for (auto& label : labels_)
                if (label != nullptr && label->getName() == slider.getName())
                    return label.get();
            return nullptr;
        }

        void layoutColumn(juce::Rectangle<int> area, std::initializer_list<juce::Slider*> sliders)
        {
            auto rows = area.reduced(8);
            const auto n = static_cast<int>(sliders.size());
            const auto sectionHeight = rows.getHeight() / std::max(1, n);
            for (auto* slider : sliders)
            {
                auto row = rows.removeFromTop(sectionHeight);
                if (auto* label = findLabelForSlider(*slider))
                {
                    label->setBounds(row.removeFromLeft(90).reduced(4, 0));
                    slider->setBounds(row.reduced(4, 6));
                }
                else
                {
                    slider->setBounds(row.reduced(4, 6));
                }
            }
        }

        VocalSuiteAudioProcessor& processor;
        juce::Label titleLabel_;
        juce::TextButton neutralPresetButton_;
        juce::TextButton warmPresetButton_;
        juce::TextButton brightPresetButton_;
        juce::ToggleButton bypassButton_;
        juce::Slider inputGainSlider_;
        juce::Slider outputGainSlider_;
        juce::Slider mixSlider_;
        juce::Slider driveSlider_;
        juce::Slider deEssSlider_;
        juce::Slider lowShelfSlider_;
        juce::Slider presenceSlider_;
        juce::Slider airSlider_;
        juce::Slider compressorSlider_;
        juce::Slider limiterSlider_;
        juce::Slider widthSlider_;
        std::vector<std::unique_ptr<juce::Label>> labels_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inputGainAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outputGainAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> deEssAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowShelfAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> presenceAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> airAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> compressorAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> limiterAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> widthAttachment_;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;
    };
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalSuiteAudioProcessor();
}
