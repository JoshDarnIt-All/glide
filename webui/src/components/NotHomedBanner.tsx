import { WarnIcon } from "../icons";

export function NotHomedBanner() {
  return (
    <div class="banner-warn">
      <WarnIcon size={20} color="var(--warn)" />
      <div>
        <div class="banner-warn-title">Homing needed this session</div>
        <div class="banner-warn-body">
          Jog to a safe reference point, then set home to unlock motion.
        </div>
      </div>
    </div>
  );
}
