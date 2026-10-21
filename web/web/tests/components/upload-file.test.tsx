import { afterEach, expect, test, vi } from "vite-plus/test";
import {
  cleanup,
  fireEvent,
  render,
  screen,
  waitFor,
} from "@testing-library/react";
import { UploadFile } from "../../src/components/UploadFile";
import { MAX_MEDIA_FILE_SIZE } from "../../src/utils/mediaUpload";
afterEach(cleanup);

test("Upload file opens, cancels and reopens", async () => {
  const select = vi.fn();
  render(<UploadFile onFileSelect={select} />);
  fireEvent.click(screen.getByRole("button", { name: "Upload file" }));
  expect(screen.getByRole("dialog")).toBeTruthy();
  expect(screen.getByText(/Maximum file size: 8 GB/)).toBeTruthy();
  fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
  expect(select).not.toHaveBeenCalled();
  await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
  fireEvent.click(screen.getByRole("button", { name: "Upload file" }));
  await waitFor(() =>
    expect(screen.getByLabelText("Choose media file")).toBeTruthy(),
  );
});

for (const [name, type, size] of [
  ["large.mp4", "video/mp4", MAX_MEDIA_FILE_SIZE],
  ["image.png", "image/png", 1],
] as const) {
  test(`Upload file accepts ${name} and closes`, async () => {
    const select = vi.fn();
    render(<UploadFile onFileSelect={select} />);
    fireEvent.click(screen.getByRole("button", { name: "Upload file" }));
    const file = new File(["media"], name, { type });
    Object.defineProperty(file, "size", { value: size });
    fireEvent.change(screen.getByLabelText("Choose media file"), {
      target: { files: [file] },
    });
    await waitFor(() => expect(select).toHaveBeenCalledWith(file));
    await waitFor(() => expect(screen.queryByRole("dialog")).toBeNull());
  });
}

test("Upload file rejects above 8 GB and permits retry", async () => {
  const select = vi.fn();
  render(<UploadFile onFileSelect={select} />);
  fireEvent.click(screen.getByRole("button", { name: "Upload file" }));
  const file = new File(["media"], "large.mp4", { type: "video/mp4" });
  Object.defineProperty(file, "size", { value: MAX_MEDIA_FILE_SIZE + 1 });
  fireEvent.change(screen.getByLabelText("Choose media file"), {
    target: { files: [file] },
  });
  await waitFor(() =>
    expect(screen.getByRole("alert").textContent).toMatch(/8 GB/),
  );
  expect(select).not.toHaveBeenCalled();
  const valid = new File(["media"], "valid.mp4", { type: "video/mp4" });
  fireEvent.change(screen.getByLabelText("Choose media file"), {
    target: { files: [valid] },
  });
  await waitFor(() => expect(select).toHaveBeenCalledWith(valid));
});
