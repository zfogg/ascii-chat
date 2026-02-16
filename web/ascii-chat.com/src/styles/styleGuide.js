/**
 * ascii-chat Website Style Guide & Toolset
 *
 * Centralized styling system for consistent, maintainable design.
 * All colors, spacing, typography, and component patterns defined here.
 */

// ============================================================================
// COLOR PALETTE
// ============================================================================
export const colors = {
  // Backgrounds
  bg: {
    darkest: "bg-gray-950", // Main page background
    dark: "bg-gray-900", // Containers, cards
    medium: "bg-gray-800", // Hover states
    light: "bg-gray-700", // Lesser elements
  },

  // Text
  text: {
    primary: "text-gray-100", // Main content
    secondary: "text-gray-300", // Supporting text
    muted: "text-gray-400", // Metadata, hints
  },

  // Brand colors (consistent with existing design)
  brand: {
    cyan: "cyan-400",
    purple: "purple-400",
    teal: "teal-400",
    pink: "pink-400",
    green: "green-400",
  },

  // Semantic colors
  semantic: {
    success: "green",
    warning: "yellow",
    error: "red",
    info: "cyan",
  },
};

// ============================================================================
// SPACING SYSTEM
// ============================================================================
export const spacing = {
  xs: "px-2 py-1",
  sm: "px-3 py-2",
  md: "px-4 py-3",
  lg: "px-6 py-4",
  xl: "px-8 py-6",
};

export const gapSize = {
  xs: "gap-2",
  sm: "gap-3",
  md: "gap-4",
  lg: "gap-6",
  xl: "gap-8",
};

export const marginSize = {
  xs: "mb-2",
  sm: "mb-3",
  md: "mb-4",
  lg: "mb-6",
  xl: "mb-8",
  section: "mb-12 sm:mb-16",
};

// ============================================================================
// COMPONENT STYLES (Reusable Tailwind Class Sets)
// ============================================================================

/**
 * Section heading styles
 * Usage: <h2 className={styles.heading.h2}>
 */
export const heading = {
  h1: "text-4xl sm:text-5xl md:text-6xl font-bold",
  h2: "text-2xl sm:text-3xl font-bold border-b border-gray-700 pb-2",
  h3: "text-lg sm:text-xl font-semibold",
  h4: "text-lg font-semibold",
};

/**
 * Card styles for containers
 * Usage: <div className={styles.card.standard}>
 */
export const card = {
  standard: "bg-gray-900/50 border border-gray-800 rounded-lg p-4 sm:p-6",
  interactive:
    "bg-gray-900/50 border border-gray-800 rounded-lg p-4 hover:border-gray-700/50 transition-colors cursor-pointer",
  subtle: "bg-gray-950/50 border border-gray-900 rounded-lg p-4",
};

/**
 * Border accent styles for colored cards
 * Usage: <div className={styles.card.standard + ' ' + styles.accent.cyan}>
 */
export const accent = {
  cyan: "border-cyan-900/30",
  purple: "border-purple-900/30",
  teal: "border-teal-900/30",
  pink: "border-pink-900/30",
  green: "border-green-900/30",
  yellow: "border-yellow-900/30",
  red: "border-red-900/30",
};

/**
 * Text color by accent
 */
export const accentText = {
  cyan: "text-cyan-300",
  purple: "text-purple-300",
  teal: "text-teal-300",
  pink: "text-pink-300",
  green: "text-green-300",
  yellow: "text-yellow-300",
  red: "text-red-300",
};

/**
 * Code/pre-formatted text styles
 */
export const code = {
  inline: "bg-gray-950 px-2 py-1 rounded font-mono text-sm text-teal-300",
  block:
    "bg-gray-900 border border-gray-800 rounded-lg p-4 overflow-x-auto font-mono text-sm",
  blockCode: "text-teal-300",
  blockText: "text-gray-400 text-sm",
};

/**
 * List styles
 */
export const list = {
  disc: "list-disc list-inside space-y-2 text-gray-300",
  decimal: "list-decimal list-inside space-y-2 text-gray-300",
  nested: "ml-4",
};

/**
 * Badge/tag styles
 */
export const badge = {
  primary:
    "inline-block bg-cyan-600/20 border border-cyan-600/50 text-cyan-300 px-3 py-1 rounded-full text-xs font-semibold",
  secondary:
    "inline-block bg-gray-800 border border-gray-700 text-gray-300 px-3 py-1 rounded-full text-xs font-semibold",
  warning:
    "inline-block bg-yellow-900/20 border border-yellow-600/50 text-yellow-300 px-3 py-1 rounded-full text-xs font-semibold",
  error:
    "inline-block bg-red-900/20 border border-red-600/50 text-red-300 px-3 py-1 rounded-full text-xs font-semibold",
};

/**
 * Info box styles
 */
export const infoBox = {
  note: "bg-cyan-900/20 border border-cyan-700/50 rounded-lg p-4",
  warning: "bg-yellow-900/20 border border-yellow-700/50 rounded-lg p-4",
  error: "bg-red-900/20 border border-red-700/50 rounded-lg p-4",
  success: "bg-green-900/20 border border-green-700/50 rounded-lg p-4",
  info: "bg-purple-900/20 border border-purple-700/50 rounded-lg p-4",
};

export const infoBoxText = {
  note: "text-cyan-300",
  warning: "text-yellow-300",
  error: "text-red-300",
  success: "text-green-300",
  info: "text-purple-300",
};

/**
 * Button styles
 */
export const button = {
  primary:
    "inline-block bg-cyan-600 hover:bg-cyan-500 text-white font-semibold px-6 py-3 rounded-lg transition-colors",
  secondary:
    "inline-block bg-gray-800 hover:bg-gray-700 text-gray-100 font-semibold px-6 py-3 rounded-lg transition-colors border border-gray-700",
};

/**
 * Link styles
 */
export const link = {
  standard: "text-cyan-400 hover:text-cyan-300 transition-colors",
  underline: "text-cyan-400 hover:text-cyan-300 transition-colors underline",
  card: "text-gray-400 hover:text-cyan-300 transition-colors",
};

/**
 * Grid layouts
 */
export const grid = {
  cols2: "grid sm:grid-cols-2 gap-4 sm:gap-6",
  cols3: "grid sm:grid-cols-3 gap-4 sm:gap-6",
  responsive: "grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4 sm:gap-6",
};

// ============================================================================
// DOCUMENTATION-SPECIFIC STYLES
// ============================================================================

export const docsSection = {
  container: "max-w-4xl mx-auto px-4 sm:px-6 py-8 sm:py-12",
  heading: `text-2xl sm:text-3xl font-bold border-b border-gray-700 pb-2 ${marginSize.lg}`,
  subheading: `text-xl font-semibold ${marginSize.md}`,
  paragraph: `text-gray-300 ${marginSize.md}`,
  space: "space-y-6 sm:space-y-8",
};

/**
 * Table styles for reference documentation
 */
export const table = {
  wrapper: "overflow-x-auto mb-6",
  base: "w-full border-collapse",
  header: "bg-gray-800 text-gray-100 font-semibold",
  headerCell: "border border-gray-700 px-4 py-2 text-left",
  bodyCell: "border border-gray-800 px-4 py-2 text-gray-300",
  alternateRow: "bg-gray-900/30",
};

// ============================================================================
// RESPONSIVE UTILITIES
// ============================================================================

export const responsive = {
  containerPadding: "px-4 sm:px-6 lg:px-8",
  maxWidth: "max-w-4xl",
  marginAuto: "mx-auto",
  fullWidth: "w-full",
};

// ============================================================================
// ANIMATION/TRANSITION UTILITIES
// ============================================================================

export const transition = {
  fast: "transition-colors duration-150",
  normal: "transition-colors duration-200",
  slow: "transition-colors duration-300",
};

// ============================================================================
// HELPER FUNCTION: Combine styles
// ============================================================================

/**
 * Combine multiple style classes safely
 * Usage: combineStyles(card.standard, accent.cyan, 'custom-class')
 */
export function combineStyles(...classes) {
  return classes.filter(Boolean).join(" ");
}

/**
 * Create accent card with border color
 * Usage: createAccentCard('cyan')
 */
export function createAccentCard(accentColor = "cyan") {
  return {
    className: combineStyles(card.standard, `border-${accentColor}-900/30`),
  };
}

/**
 * Create colored section heading
 * Usage: createSectionHeading('purple', 'Section Title')
 */
export function createSectionHeading(color = "cyan", title) {
  return {
    text: title,
    className: combineStyles(heading.h2, `text-${color}-400`),
  };
}

export default {
  colors,
  spacing,
  gapSize,
  marginSize,
  heading,
  card,
  accent,
  accentText,
  code,
  list,
  badge,
  infoBox,
  infoBoxText,
  button,
  link,
  grid,
  docsSection,
  table,
  responsive,
  transition,
  combineStyles,
  createAccentCard,
  createSectionHeading,
};
