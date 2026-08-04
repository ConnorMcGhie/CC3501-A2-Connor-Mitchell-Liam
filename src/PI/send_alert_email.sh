# #!/bin/bash
# # send_alert_email.sh - emails a PlantMonitor alert to your phone.
# #
# # Sends via SMTP directly using curl

# #
# # Is called automatically by PlantMonitor:
# #   ./send_alert_email.sh "the alert message"

# # Email provider's SMTP server + port
# #   Gmail:   smtp.gmail.com:465
# #   Outlook: smtp.office365.com:587
# #   Yahoo:   smtp.mail.yahoo.com:465
# SMTP_SERVER="smtp.gmail.com:465"

# # The account curl logs in as to send the mail.
# FROM_EMAIL="connormmcghie@gmail.com"

# # APP PASSWORD, not the normal login password. For Gmail:
# #   1. Turn on 2-Step Verification: myaccount.google.com/security
# #   2. Create an App Password: myaccount.google.com/apppasswords
# #   3. Paste the 16-character password below.

# APP_PASSWORD="**** **** **** ****" # Not giving connors password to you Luke!

# # Where the alert is sent.
# TO_EMAIL="connormmcghie@gmail.com"

# MESSAGE="$1"
# if [ -z "$MESSAGE" ]; then
#     echo "Usage: $0 \"alert message\"" >&2
#     exit 1
# fi

# SUBJECT="PlantMonitor Alert"

# EMAIL_BODY=$(cat <<EOF
# From: ${FROM_EMAIL}
# To: ${TO_EMAIL}
# Subject: ${SUBJECT}
# Date: $(date -R)

# ${MESSAGE}
# EOF
# )

# echo "${EMAIL_BODY}" | curl --silent --show-error \
#     --url "smtps://${SMTP_SERVER}" \
#     --ssl-reqd \
#     --mail-from "${FROM_EMAIL}" \
#     --mail-rcpt "${TO_EMAIL}" \
#     --user "${FROM_EMAIL}:${APP_PASSWORD}" \
#     --upload-file -
