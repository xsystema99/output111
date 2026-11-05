#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>

/// Colours, fonts and the few custom widgets of the editor (dark theme, rounded cards, per-degree colours).
namespace ui {

inline juce::Colour backgroundColour() { return juce::Colour(0xff0b0b0d); }
inline juce::Colour panelBorderColour() { return juce::Colour(0xff2a2a2f); }
inline juce::Colour controlFillColour() { return juce::Colour(0xff121215); }
inline juce::Colour controlOutlineColour() { return juce::Colour(0xff3a3a40); }
inline juce::Colour textColour() { return juce::Colour(0xfff1f1f3); }
inline juce::Colour mutedTextColour() { return juce::Colour(0xff9a9aa3); }

inline juce::Font font(float height, bool bold = false) {
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

/// One colour per scale degree (1..7), shared by the timeline cards and the palette.
inline juce::Colour degreeColour(int degree) {
    static const juce::uint32 colours[7] = {0xffd6605a,   // 1 red
                                            0xffdb9b48,   // 2 orange
                                            0xffb8a248,   // 3 gold
                                            0xff68a868,   // 4 green
                                            0xff4fa0be,   // 5 blue
                                            0xff7e6ac4,   // 6 purple
                                            0xffb874ac};  // 7 pink
    return juce::Colour(colours[std::clamp(degree, 1, 7) - 1]);
}

/// Roman numeral as shown to people: the parser's ASCII "o" for diminished becomes the degree sign.
inline juce::String displayNumeral(const juce::String& ascii) { return ascii.replace("o", juce::String::fromUTF8("\xC2\xB0")); }

class StyledButton final : public juce::Button {
public:
    enum class Look { Palette, Pill, Outline, Primary };

    StyledButton(const juce::String& label, Look lookStyle, float fontHeight = 14.0f)
        : juce::Button(label), look(lookStyle), textHeight(fontHeight) {}

    void setPaletteColour(juce::Colour colour) {
        paletteColour = colour;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override {
        const auto area = getLocalBounds().toFloat().reduced(1.0f);
        const float alpha = isEnabled() ? 1.0f : 0.35f;
        const bool on = getToggleState();
        juce::Colour labelColour = textColour();

        switch (look) {
            case Look::Palette: {
                auto fill = paletteColour;
                if (down) fill = fill.darker(0.2f);
                else if (highlighted) fill = fill.brighter(0.12f);
                g.setColour(fill.withMultipliedAlpha(alpha));
                g.fillRoundedRectangle(area, 10.0f);
                if (on) {
                    g.setColour(juce::Colours::white.withMultipliedAlpha(alpha));
                    g.drawRoundedRectangle(area.reduced(1.0f), 9.0f, 2.0f);
                }
                labelColour = juce::Colours::white;
                break;
            }
            case Look::Pill: {
                const float radius = area.getHeight() * 0.5f;
                if (on) {
                    g.setColour(juce::Colour(0xff3b3b40).withMultipliedAlpha(alpha));
                    g.fillRoundedRectangle(area, radius);
                } else if (highlighted) {
                    g.setColour(juce::Colour(0xff1a1a1e).withMultipliedAlpha(alpha));
                    g.fillRoundedRectangle(area, radius);
                }
                g.setColour(controlOutlineColour().withMultipliedAlpha(alpha));
                g.drawRoundedRectangle(area, radius, 1.0f);
                break;
            }
            case Look::Outline: {
                if (highlighted || down) {
                    g.setColour(juce::Colour(0xff1a1a1e).withMultipliedAlpha(alpha));
                    g.fillRoundedRectangle(area, 10.0f);
                }
                g.setColour(controlOutlineColour().withMultipliedAlpha(alpha));
                g.drawRoundedRectangle(area, 10.0f, 1.0f);
                break;
            }
            case Look::Primary: {
                auto fill = textColour();
                if (down) fill = fill.darker(0.25f);
                else if (highlighted) fill = fill.darker(0.08f);
                g.setColour(fill.withMultipliedAlpha(alpha));
                g.fillRoundedRectangle(area, 10.0f);
                labelColour = juce::Colour(0xff0b0b0d);
                break;
            }
        }

        g.setColour(labelColour.withMultipliedAlpha(alpha));
        g.setFont(font(textHeight, true));
        g.drawFittedText(getButtonText(), getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
    }

private:
    Look look;
    float textHeight;
    juce::Colour paletteColour{0xff555555};
};

}  // namespace ui
