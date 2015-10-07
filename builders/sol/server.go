package main

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"os/exec"
	"strconv"
	"strings"
)

// TODO: HERE: improving error messages
// TODO: HERE: normalize with zip output?

func runServer() {
	addr := ":2222"
	fmt.Println("Listening at", addr)
	http.HandleFunc("/build/", handleBuild)
	http.HandleFunc("/list", handleList)
	log.Fatal(http.ListenAndServe(addr, nil))
}

func listPlatforms() []string {
	dir, _ := os.Open(initialDir)
	fis, _ := dir.Readdir(0)

	var platforms []string
	for _, fi := range fis {
		if fi.IsDir() && strings.HasPrefix(fi.Name(), "platform-") {
			platforms = append(platforms, fi.Name())
		}
	}

	dir.Close()
	return platforms
}

func handleList(w http.ResponseWriter, r *http.Request) {
	for _, p := range listPlatforms() {
		fmt.Fprintln(w, p)
	}
}

func platformExists(platform string) bool {
	for _, p := range listPlatforms() {
		if p == platform {
			return true
		}
	}
	return false
}

func clientError(w http.ResponseWriter, format string, a ...interface{}) {
	fmt.Print("Request failed: ")
	fmt.Printf(format, a...)
	fmt.Println()
	fmt.Fprintf(w, format, a...)
	fmt.Fprintln(w)
}

func internalError(w http.ResponseWriter, format string, a ...interface{}) {
	fmt.Print("Internal error: ")
	fmt.Printf(format, a...)
	fmt.Println()
	fmt.Fprintln(w, "Internal error")
}

func compile(dir string, platform string) (*bytes.Buffer, error) {
	var out bytes.Buffer
	mw := io.MultiWriter(&out, os.Stdout)

	// TODO: check permission denied error

	cmd := exec.Command(initialDir + "/" + platform + "/compile")
	cmd.Dir = dir
	cmd.Stdout = mw
	cmd.Stderr = mw

	return &out, cmd.Run()
}

func handleBuild(w http.ResponseWriter, r *http.Request) {
	plat := r.URL.Path[7:]

	if !platformExists(plat) {
		clientError(w, "%s is not available", plat)
		return
	}

	dir, err := ioutil.TempDir("", "sb-")
	if err != nil {
		internalError(w, "couldn't create tempdir: %s", err)
		return
	}

	defer os.RemoveAll(dir)

	tr := tar.NewReader(r.Body)

	header, _ := tr.Next()
	for ; header != nil; header, _ = tr.Next() {
		switch header.Typeflag {
		case tar.TypeDir:
			os.MkdirAll(dir+"/"+header.Name, 0755)

		case tar.TypeReg:
			f, err := os.Create(dir + "/" + header.Name)
			if err != nil {
				log.Fatal(err)
			}
			io.Copy(f, tr)
			f.Close()
		}
	}

	out, err := compile(dir, plat)

	if err != nil {
		if out.Bytes() != nil {
			clientError(w, "Compilation error\n")
			io.Copy(w, out)
		} else {
			internalError(w, "couldn't call compile: %s", err)
		}
	} else {
		bin, err := os.Open(dir + "/output.zip")
		if err != nil {
			// TODO: set HTTP header.
			fmt.Fprintf(w, "Internal error: %s\n", err)
			return
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		stat, _ := bin.Stat()
		fmt.Println(strconv.FormatInt(stat.Size(), 10))
		io.Copy(os.Stdout, out)
		w.Header().Set("Content-Length", strconv.FormatInt(stat.Size(), 10))
		io.Copy(w, bin)
	}
}
