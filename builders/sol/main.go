package main

import (
	"flag"
	"os"
)

var runAsServer = flag.Bool("run-as-server", false, "Run as build server")
var connect = flag.String("connect", "localhost", "Server to connect")
var initialDir string

// TODO: more info on the output, correct file name for output, automatically zip?
// TODO: banner?
// TODO: be crazier with error checks

func main() {
	flag.Parse()

	initialDir, _ = os.Getwd()

	if *runAsServer {
		runServer()
	} else {
		runClient()
	}
}
