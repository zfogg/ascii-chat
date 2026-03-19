import type { HTMLAttributes } from "react";
import { createElement, useEffect, useState } from "react";
import { useHeadingContext } from "./HeadingContext";

/**
 * Convert heading text to a URL-friendly ID
 */
function slugify(text: string): string {
  return text
    .toLowerCase()
    .trim()
    .replace(/[^\w\s-]/g, "")
    .replace(/[\s_]+/g, "-")
    .replace(/--+/g, "-")
    .replace(/^-+|-+$/g, "");
}

interface HeadingProps extends Omit<
  HTMLAttributes<HTMLHeadingElement>,
  "className"
> {
  level?: 1 | 2 | 3 | 4;
  className?: string;
  id?: string;
  anchorLink?: boolean;
}

export function Heading({
  level = 1,
  children,
  className = "",
  id,
  anchorLink = true,
  ...props
}: HeadingProps) {
  const baseClass = `heading-${level}`;
  const headingContext = useHeadingContext();
  const [finalId, setFinalId] = useState<string | undefined>(undefined);

  // Auto-generate ID from children if not provided
  const baseId =
    id || (typeof children === "string" ? slugify(children) : undefined);

  // Register with context to ensure uniqueness
  useEffect(() => {
    if (baseId) {
      const uniqueId = headingContext.registerHeading(baseId);
      setFinalId(uniqueId);
    }
  }, [baseId, headingContext]);

  const combinedClassName = `${baseClass} ${className}`.trim();

  const content =
    anchorLink && finalId
      ? createElement(
          "a",
          {
            href: `#${finalId}`,
            className:
              "group inline-flex items-center gap-2 hover:opacity-75 transition-opacity",
            title: "Click to go to this section",
          },
          children,
          createElement(
            "span",
            {
              className:
                "opacity-0 group-hover:opacity-50 transition-opacity text-xs",
            },
            "🔗",
          ),
        )
      : children;

  return createElement(
    `h${level}`,
    {
      className: combinedClassName,
      id: finalId,
      ...props,
    },
    content,
  );
}
