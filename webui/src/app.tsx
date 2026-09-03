import { useEffect } from "preact/hooks";
import { connectWebSocket, connected } from "./store";
import { activeTab, editingPreset } from "./ui";
import { ControlScreen, StopBar } from "./screens/ControlScreen";
import { PresetsScreen } from "./screens/PresetsScreen";
import { PresetEditorScreen } from "./screens/PresetEditorScreen";
import { SetupScreen } from "./screens/SetupScreen";
import { DeviceScreen } from "./screens/DeviceScreen";
import { TabBar } from "./components/TabBar";
import { ConnectionOverlay } from "./components/ConnectionOverlay";
import { ErrorToast } from "./components/ErrorToast";

export function App() {
  useEffect(() => {
    connectWebSocket();
  }, []);

  const tab = activeTab.value;
  const inEditor = tab === "presets" && editingPreset.value !== null;

  let content;
  if (tab === "control") content = <ControlScreen />;
  else if (tab === "presets") content = inEditor ? <PresetEditorScreen /> : <PresetsScreen />;
  else if (tab === "setup") content = <SetupScreen />;
  else content = <DeviceScreen />;

  return (
    <div class="app-shell">
      <div
        class={connected.value ? undefined : "content-degraded"}
        style={{ display: "flex", flexDirection: "column", flex: 1, minHeight: 0 }}
      >
        {content}
        {tab === "control" && <StopBar />}
        {!inEditor && <TabBar />}
      </div>
      {!connected.value && <ConnectionOverlay />}
      <ErrorToast />
    </div>
  );
}
