# Semaphore Monitor

This is a set of samples to get the Soletta Semaphore build status and
show it.

 * semaphore-monitor.fbp: the logic that can be reused for all
   projects and outputs. It takes an URL and once the input port
   **UPDATE** receives a packet it will do the HTTP request and output
   the status string on its **STATUS** port.

 * semaphore-monitor-console.fbp: uses the logic above with a 5s
   timer, output goes to console.

## Running


```sh
./semaphore-monitor-console.fbp # runs forever, use Ctrl-C to quit
```
