# Instant Highlight Source Draw

**Instant Highlight Source Draw** is a fast, intuitive OBS Studio plugin that lets you draw directly on a source in real time to highlight important elements during your livestream or recording.

Draw shapes such as **squares, circles, arrows, and hearts**, or erase parts of the drawing instantly — all from a dedicated dock designed for speed and ease of use.

---

## ✨ Features

- 🖊️ **Real-time drawing on a source**
- 🔲 **Multiple tools**: Square, Circle, Arrow, Heart, Eraser
- 🎯 **Instant visual highlighting** for tutorials, presentations, and live commentary
- 🎨 **Color picker with opacity control**
- 📏 **Adjustable thickness (1–8)**
- ⏱️ **Release behavior modes**
  - **Keep until cleared** – drawings persist indefinitely
  - **Fade after release** – drawings fade out after a configurable time
- ↩️ **Undo** last action
- 🧹 **Clear** all drawings instantly
- 🪟 **Dedicated OBS dock** with persistent layout (restored on restart)
- 🖱️ **Source Interaction support** (draw directly via the Interact window)

---

## 🧠 How It Works

The plugin adds a new **Draw Source** to OBS.  
All drawing happens inside the source’s interaction layer, ensuring:

- Zero impact on scene structure
- No external browser sources or overlays required
- Clean separation between visuals and control logic

The **Draw Tools Dock** allows you to:

- Select the active Draw Source
- Change tools, colors, opacity, thickness
- Control release behavior
- Open the source interaction window
- Undo or clear drawings instantly

---

## 🧩 Installation

1. Download the plugin from the official page:
   👉 https://obscountdown.com
2. Extract the archive into your OBS plugins directory:
   - **Windows**:  
     `C:\Program Files\obs-studio\obs-plugins\`
3. Restart OBS Studio

---

## 🚀 Getting Started

1. Add a **Draw Source** to your scene
2. Open **View → Docks → Draw Tools**
3. Select your Draw Source from the **Target** dropdown
4. Click **Interact**
5. Start drawing directly on the source

Your drawings will appear instantly in the scene.

---

## ⚙️ Release Modes Explained

### Keep until cleared

- Drawings remain visible indefinitely
- Perfect for explanations and step-by-step walkthroughs
- Clear manually or use the Eraser tool

### Fade after release

- Drawings fade out automatically after mouse release
- Fade duration is configurable (milliseconds)
- Ideal for quick highlights during live action

---

## 🎥 Use Cases

- Live tutorials and OBS demonstrations
- Game streaming with visual callouts
- Educational streams and online classes
- Presentations and screen sharing
- VOD annotations and quick emphasis

---

## 🧪 Compatibility

- OBS Studio **28+**
- Windows (64-bit)
- Requires **Frontend API** and **Qt** (standard OBS builds)

---

## 📌 Notes

- The dock layout and visibility are automatically restored on OBS restart
- The dock ID is stable to ensure persistent UI state
- No external dependencies or browser sources required

---

## ❤️ Support & Feedback

If you find this plugin useful:

- Share it with other streamers
- Report issues or suggestions
- Support development at **https://obscountdown.com**

---

## 🧑‍💻 Author

**MMLTech**  
Website: https://obscountdown.com  
Email: contact@obscountdown.com

---

Happy streaming — and happy highlighting!
