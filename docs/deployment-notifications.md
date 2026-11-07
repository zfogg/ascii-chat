# Release and deployment notifications

`.github/workflows/notify.yml` sends the notifications for CLI releases, the
three Vercel websites, the GitHub Pages documentation, and container publication.
Each caller supplies the subject, body, and job results; the shared workflow
uses the existing SMTP secrets and `dawidd6/action-send-mail` action.

The existing `SMTP_TO_EMAIL` destination routes release notifications to
Pushover. Keep that destination unchanged to retain its configured sound and
delivery settings. The workflow does not set a sound or require a Pushover API token.
See [Pushover's email gateway documentation](https://support.pushover.net/s1-pushover/knowledgebase/top/c7-e-mail-gateway).

Notification jobs run after their deployment or publication job, including on
failure, and include a link to the workflow run. A successful notification job
confirms that the SMTP server accepted the email; device delivery and sound are
controlled by Pushover and the recipient's device settings.
