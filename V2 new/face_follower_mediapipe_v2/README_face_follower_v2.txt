# Face Follower V2 — MediaPipe

## Files
- `face_follower_client_mediapipe_v2.ipynb` — laptop/PC client using MediaPipe Face Detection.
- `esp32cam_face_follower_v2_mediapipe.ino` — ESP32-CAM sketch compatible with the V2 client; the HTTP API and motor pins are retained from the original.
- `requirements_face_follower_v2.txt` — pinned Python dependencies.

## Important install note
The original notebook shows Python 3.11.9 with OpenCV 5 / NumPy 2.4.1. MediaPipe 0.10.21 is the compatibility target used here, so the notebook pins `numpy<2` and `opencv-contrib-python<5`. Restart the Jupyter kernel after the install cell completes.

## Run order
1. Flash the Arduino sketch.
2. Open Serial Monitor at 115200 baud.
3. Copy the ESP32 IP into `ESP32_IP` in the notebook.
4. Run the install cell once, then restart the kernel.
5. Run the import/config/detector cells.
6. Run `run_face_follower(show_window=True)`.
7. Press `Q` to stop; the notebook sends a final `stop` command.

## Control behavior
The original control rules are preserved: large face -> stop; face left/right of the deadzone -> left/right; centered face -> forward. V2 adds face-box smoothing and two-frame command confirmation.

## Hardware behavior
ENA and ENB remain tied to 5V in the supplied Arduino sketch, so turns are still full-speed pivots. MediaPipe changes detection; it does not add PWM motor control.
