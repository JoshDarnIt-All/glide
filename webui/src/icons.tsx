// Every icon in the app, as inline SVG -- deliberately not an icon
// font/library (flash-budget constraint, see docs/m4_ui_plan.md's
// "Technical architecture" section). Paths lifted directly from the
// approved mockup (/tmp/glide-m4-design/*.dc.html) so the visual
// language matches exactly, not a reinterpretation.

import type { JSX } from "preact";

type IconProps = { size?: number; color?: string; class?: string };

function base(children: JSX.Element | JSX.Element[], { size = 20, color = "currentColor", class: cls }: IconProps) {
  return (
    <svg
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke={color}
      stroke-width={2}
      stroke-linecap="round"
      stroke-linejoin="round"
      class={cls}
    >
      {children}
    </svg>
  );
}

export const GlideMark = (p: IconProps) =>
  base(
    [
      <circle cx="12" cy="12" r="8" />,
      <circle cx="12" cy="12" r="2.5" fill={p.color ?? "currentColor"} stroke="none" />,
    ],
    p
  );

export const WifiIcon = (p: IconProps) =>
  base(
    [
      <path d="M5 12.5a10 10 0 0 1 14 0" />,
      <path d="M8.5 16a5 5 0 0 1 7 0" />,
      <circle cx="12" cy="19.5" r="1.2" fill={p.color ?? "currentColor"} stroke="none" />,
    ],
    p
  );

export const WarnIcon = (p: IconProps) =>
  base(
    [
      <path d="M12 9v4" />,
      <path d="M12 17h.01" />,
      <path d="M10.3 3.9 2.5 18a2 2 0 0 0 1.7 3h15.6a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z" />,
    ],
    p
  );

export const StarIcon = (p: IconProps) =>
  base(<path d="m12 2 2.9 6.3 6.9.8-5.1 4.7 1.4 6.8L12 17.3l-6.1 3.3 1.4-6.8-5.1-4.7 6.9-.8Z" />, p);

export const ArrowRightIcon = (p: IconProps) =>
  base([<path d="M5 12h14" />, <path d="m13 6 6 6-6 6" />], p);

export const ChevronRight = (p: IconProps) => base(<path d="m9 6 6 6-6 6" />, p);
export const ChevronLeft = (p: IconProps) => base(<path d="m15 6-6 6 6 6" />, p);
export const ChevronDown = (p: IconProps) => base(<path d="M6 9l6 6 6-6" />, p);

export const PlayFillIcon = ({ size = 20, color = "currentColor" }: IconProps) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill={color} stroke="none">
    <path d="M7 5.14v13.72a1 1 0 0 0 1.5.86l11.5-6.86a1 1 0 0 0 0-1.72L8.5 4.28A1 1 0 0 0 7 5.14Z" />
  </svg>
);

export const StopFillIcon = ({ size = 20, color = "currentColor" }: IconProps) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill={color} stroke="none">
    <rect x="4" y="4" width="16" height="16" rx="2" />
  </svg>
);

export const DwellIcon = (p: IconProps) =>
  base(
    [<rect x="6" y="5" width="4" height="14" rx="1" />, <rect x="14" y="5" width="4" height="14" rx="1" />],
    p
  );

export const IdleDotIcon = ({ size = 9, color = "currentColor" }: IconProps) => (
  <svg width={size} height={size} viewBox="0 0 10 10">
    <circle cx="5" cy="5" r="4" fill={color} />
  </svg>
);

export const StoppedSquareIcon = ({ size = 14, color = "currentColor" }: IconProps) => (
  <svg width={size} height={size} viewBox="0 0 24 24" fill={color} stroke="none">
    <rect x="5" y="5" width="14" height="14" rx="2" />
  </svg>
);

export const CloseIcon = (p: IconProps) => base([<path d="M18 6 6 18" />, <path d="m6 6 12 12" />], p);

export const SpinnerIcon = (p: IconProps & { class?: string }) =>
  base(<path d="M21 12a9 9 0 1 1-3.5-7.1" />, { ...p, class: `spin ${p.class ?? ""}`.trim() });

export const GridIcon = (p: IconProps) =>
  base(
    [
      <rect x="3" y="3" width="8" height="8" rx="1.5" />,
      <rect x="13" y="3" width="8" height="8" rx="1.5" />,
      <rect x="3" y="13" width="8" height="8" rx="1.5" />,
      <rect x="13" y="13" width="8" height="8" rx="1.5" />,
    ],
    p
  );

export const CalibrateIcon = (p: IconProps) =>
  base([<path d="m14.7 6.3 3 3L8 19H5v-3Z" />, <path d="m17.5 3.5 3 3" />], p);

export const EditIcon = (p: IconProps) =>
  base([<path d="M12 20h9" />, <path d="M16.5 3.5a2.1 2.1 0 0 1 3 3L7 19l-4 1 1-4Z" />], p);

export const DuplicateIcon = (p: IconProps) =>
  base(
    [
      <rect x="8" y="8" width="12" height="12" rx="2" />,
      <path d="M16 8V6a2 2 0 0 0-2-2H6a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h2" />,
    ],
    p
  );

export const TrashIcon = (p: IconProps) =>
  base(
    [
      <path d="M4 7h16" />,
      <path d="M6 7V5a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v2" />,
      <path d="M6 7v13a1 1 0 0 0 1 1h10a1 1 0 0 0 1-1V7" />,
    ],
    p
  );

export const PlusIcon = (p: IconProps) => base([<path d="M12 5v14" />, <path d="M5 12h14" />], p);

export const CheckIcon = (p: IconProps) => base(<path d="M20 6 9 17l-5-5" />, p);

export const UploadIcon = (p: IconProps) =>
  base([<path d="M12 3v13" />, <path d="m7 8 5-5 5 5" />, <path d="M5 21h14" />], p);
