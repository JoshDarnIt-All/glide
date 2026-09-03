import { GlideMark, GridIcon, CalibrateIcon, WifiIcon } from "../icons";
import { activeTab, type Tab } from "../ui";

const TABS: { id: Tab; label: string; Icon: typeof GlideMark }[] = [
  { id: "control", label: "Control", Icon: GlideMark },
  { id: "presets", label: "Presets", Icon: GridIcon },
  { id: "setup", label: "Setup", Icon: CalibrateIcon },
  { id: "device", label: "Device", Icon: WifiIcon },
];

export function TabBar() {
  const current = activeTab.value;
  return (
    <nav class="tab-bar">
      {TABS.map(({ id, label, Icon }) => {
        const isActive = current === id;
        return (
          <button
            key={id}
            class={`tab-item press ${isActive ? "active" : ""}`}
            onClick={() => (activeTab.value = id)}
            aria-current={isActive ? "page" : undefined}
          >
            <Icon size={22} color={isActive ? "var(--accent)" : "var(--text-dimmer)"} />
            <span>{label}</span>
          </button>
        );
      })}
    </nav>
  );
}
