# Mavlink Examples

These examples demonstrate how to interact with ardupilot using Soletta's
Mavlink implementation.

To run these samples one needs to have the ardupilot running, ardupilot
has a simulator(SITL) so it's possible to test these samples before
actually flying a vehicle.

## Running the samples
   * Install MAVProxy:
   ```sh
     pip install MAVProxy
   ```

   Note: If you're running fedora, install ```python-devel``` package before installing
     MAVProxy;

   * Clone the ardupilot source code:
   ```sh
     git clone https://github.com/diydrones/ardupilot.git
   ```

   * Prepare SITL(do the initial calibration):
   ```sh
     cd ardupilot/ArduCopter/ && ../Tools/autotest/sim_vehicle.sh -w
   ```

   * Run the simulation:
   ```sh
     ../Tools/autotest/sim_vehicle.sh -v ArduCopter
   ```

   * Build Soletta' samples and - from the root source folder - either run:
   ```sh
     ./build/stage/samples/mavlink/mavlink-goto tcp:localhost:5762
   ```

   * Or run:
   ```sh
     ./build/stage/samples/mavlink/basic tcp:localhost:5762
   ```