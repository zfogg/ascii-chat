import { ReactNode } from "react";
import { Heading } from "./Heading";
import { Button } from "./Button";

interface NotFoundProps {
  backButtonText?: string;
  headingText?: string;
  descriptionText?: string;
  footer?: ReactNode;
  containerClassName?: string;
  headingClassName?: string;
  descriptionClassName?: string;
  buttonClassName?: string;
  preClassName?: string;
}

export function NotFound({
  backButtonText = "← Back to home",
  headingText = "Page not found",
  descriptionText = "The page you're looking for doesn't exist or has been moved.",
  footer,
  containerClassName = "flex-1 flex items-center justify-center p-4",
  headingClassName = "text-2xl font-bold mb-2",
  descriptionClassName = "text-gray-400 mb-8",
  buttonClassName = "inline-block px-6 py-3 bg-cyan-500 text-black rounded hover:opacity-80 transition-opacity font-mono",
  preClassName = "text-6xl font-mono mb-4 overflow-hidden",
}: NotFoundProps = {}) {
  return (
    <div className={containerClassName}>
      <div className="text-center max-w-md">
        <pre className={preClassName}>404</pre>
        <Heading level={1} className={headingClassName}>
          {headingText}
        </Heading>
        <p className={descriptionClassName}>{descriptionText}</p>
        <Button href="/" className={buttonClassName}>
          {backButtonText}
        </Button>
        {footer && <div className="mt-8">{footer}</div>}
      </div>
    </div>
  );
}
