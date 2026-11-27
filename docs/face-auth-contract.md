# Face verification behavior, updated 2026-11-27

Version 1.3.5 uses the native Qt QR dialog for a `realname` challenge from code 60043. The server confirms the scan through the current challenge's status endpoint.

## Bilibili Live Center

The [official live center](https://link.bilibili.com/p/center/index) inspected on November 26 served its November 19 build with the following application chunks:

- [Start-live component](https://s1.hdslb.com/bfs/static/blive/blfe-link-center/static/js/85.5bea369d4a462165a959.js)
- [Room face component](https://s1.hdslb.com/bfs/static/blive/blfe-link-center/static/js/419.a00c91c52ee245fb38f2.js)
- [New-anchor face component](https://s1.hdslb.com/bfs/static/blive/blfe-link-center/static/js/504.2512263adc234da7fe6f.js)

For 60024 and 60028, the start-live component uses the QR URL returned by the start response and polls `IsUserIdentifiedByFaceAuth` with the room ID and original response code every 1500 ms. One truthy `data.is_identified` closes the official face dialog.

For 60043, the component loads [CaptchaLoader.js](https://s1.hdslb.com/bfs/seed/jinkela/risk-captcha-sdk/CaptchaLoader.js), calls `CaptchaLoader.load()`, then invokes the verifier with the current start attempt's `data.risk_extra.v_voucher`.

The loader fetches its logic and resources from the [official risk config](https://api.bilibili.com/x/web-frontend/risk/config). On success, the Live Center SDK resolves its verification and writes a short-lived `x-bili-gaia-vtoken` cookie.

## Plugin behavior

- Room verification keeps one QR dialog, polls every 1.5 seconds, and automatically attempts to start after a positive reply. Subsequent positive replies can trigger a retry after 5, 10, 20 and then 30 seconds. The ten-minute session limit and manual retry apply throughout the room challenge.
- For 60043, the API worker registers the current `v_voucher` on Bilibili's `/x/gaia-vgate/v2/register` endpoint and decodes the response as implemented by StartLive 1.4.0. A `realname` challenge produces a mobile Bilibili QR and a polling token. The `validatePreCheck` poll runs through the same configured API route. [Registration worker](https://github.com/Radekyspec/StartLive/blob/1.4.0/startlive/core/workers/face_auth/face_captcha.py) · [Polling worker](https://github.com/Radekyspec/StartLive/blob/1.4.0/startlive/core/workers/face_auth/face_auth.py)
- A numeric status of 1 confirms verification and triggers a fresh start request. Status 0 keeps the QR waiting. Statuses 2 and higher stop polling and prompt the user to get a new QR. Other challenge types, missing vouchers, and registration failures offer the official Live Center.
- Each retry fetches the server-reported Windows livestream client version. A version lookup timeout retains manual retry in the same window. If a fresh start returns another challenge, the plugin uses the new challenge. OBS output requires a successful start reply with valid stream credentials.
- An explicit signed-out or expired-login API reply closes the verification window and offers a fresh login QR. Challenge expiration and transport errors remain part of the verification flow.

API and Qt fixtures cover the voucher codec, pending status, confirmation, expiration, other challenge types, and manual retry after version timeout. Confirm the live account's scan and subsequent start in OBS separately.
