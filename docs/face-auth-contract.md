# Face verification behavior, updated 2026-10-24

Version 1.3.3 removes the embedded official-risk browser following a reproduced blank-window regression. At the user's explicit request, code 60043 again opens the native QR dialog using the earlier face-middle entry. The restored QR was generated successfully with a real account configuration during comparison. This confirms QR display, not completion of a human-verification challenge or a subsequent broadcast.

## Historical first-party evidence (2026-10-20/21)

The [official live center](https://link.bilibili.com/p/center/index) inspected at that time identified build 2026.10.15 and loaded the following application chunks:

- [Start-live component](https://s1.hdslb.com/bfs/static/blive/blfe-link-center/static/js/87.7d7e2d6e7b4148a88dc1.js)
- [Room face component](https://s1.hdslb.com/bfs/static/blive/blfe-link-center/static/js/418.5a8fc1a42dc116cc6aea.js)
- [New-anchor face component](https://s1.hdslb.com/bfs/static/blive/blfe-link-center/static/js/503.cc08cc5fe7ab74003038.js)

The start-live component distinguishes three response codes. For 60024 and 60028 it uses the QR URL returned by the start response, retains the original response code and polls `POST https://api.live.bilibili.com/xlive/app-blink/v1/preLive/IsUserIdentifiedByFaceAuth` with room ID and that code every 1500 ms. One truthy `data.is_identified` closes the face dialog. This is polling, not a desktop push notification.

For 60043 it does **not** use that dialog or endpoint. It loads [CaptchaLoader.js](https://s1.hdslb.com/bfs/seed/jinkela/risk-captcha-sdk/CaptchaLoader.js), calls `CaptchaLoader.load()`, then invokes the resulting verifier with `riskParams.v_voucher` from `data.risk_extra.v_voucher` of this start attempt.

The loader obtains current logic/resources from [the official risk config](https://api.bilibili.com/x/web-frontend/risk/config). At inspection the logic was [CaptchaLoader.logic.d883af7c.js](https://s1.hdslb.com/bfs/static/jinkela/risk-captcha-sdk/CaptchaLoader.logic.d883af7c.js). For live-detection challenges it uses per-challenge tokens, a dedicated mobile live-detection URL, v2 validatePreCheck polling at 2000 ms, then server validation. Successful verification resolves a token and stores a short-lived `x-bili-gaia-vtoken` cookie. These token encoding/decoding and challenge-type details stay inside the unmodified official SDK; the plugin does not recreate them.

The [legacy mobile middle page](https://www.bilibili.com/blackboard/live/face-auth-middle.html) opens the app's user-center face verification scheme. It does not carry the SDK's per-attempt voucher. Restoring that compatibility entry does not establish equivalence with the official risk SDK or guarantee that every 60043 challenge will accept it.

## Plugin changes and boundaries

- Room polling is single-flight, at 1500 ms. One explicit valid success retries start immediately; a still-positive stale response cannot repeatedly issue starts for a consumed result. A new pending response rearms automatic completion; a manual retry remains available.
- 60024 and 60028 continue to render the server-returned QR URL. For 60043, the plugin locally renders the earlier face-middle URL with its account parameter encoded. No embedded browser or SDK download is involved in rendering that QR.
- The native path preserves the response's original code, including 60043, when querying the legacy face-status endpoint. This intentionally restores the older compatibility behavior; the historical SDK evidence above does not validate that endpoint for every risk challenge.
- A positive status only permits another start-live request. If the server still requires verification, the same native dialog remains open and stale-positive suppression prevents repeated automatic starts. Manual retry remains available. OBS output requires actual start success with valid streaming credentials.
- The WebView2 host, HTTP CONNECT adapter, SDK completion-token injection and build/runtime dependency are removed. Normal API proxy routing remains unchanged.

## Validation distinction

API tests cover 60024/60028/60043 classification, the restored QR with or without an SDK voucher, encoded account parameters, original polling codes, unknown-code rejection and clearing challenge state on confirmed start. Qt tests assert native QR rendering, replacement within the same dialog, first-positive retry, stale-positive suppression and cancellation.

The removed browser tests established only SDK loading and rejection of a synthetic voucher; they did not establish real-account QR rendering. A later same-configuration comparison reproduced the old native QR and the new browser flow's failure to show it. The user explicitly requested removing the browser flow and submitting the restored native path.

Real human-verification completion followed by a successful start and intended OBS output remains unverified. QR display, offline regressions and builds must not be reported as that end-to-end observation.
