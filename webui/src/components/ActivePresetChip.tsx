import { StarIcon } from "../icons";

export function ActivePresetChip({ name }: { name: string | null }) {
  const hasPreset = Boolean(name);
  return (
    <div class="chip-preset" style={{ borderColor: hasPreset ? "var(--accent)" : "var(--border)" }}>
      <StarIcon size={15} color={hasPreset ? "var(--accent)" : "var(--text-dimmer)"} />
      <span
        class="chip-preset-label"
        style={{ color: hasPreset ? "var(--text)" : "var(--text-dimmer)" }}
      >
        {hasPreset ? name : "No preset active"}
      </span>
    </div>
  );
}
