/**
 * Copyright 2020-present Facebook. All Rights Reserved.
 *
 * This program file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program in a file named COPYING; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

package flash

import (
	"bytes"
	"log"
	"os"
	"strings"
	"testing"

	"github.com/facebook/openbmc/tools/flashy/lib/step"
	"github.com/facebook/openbmc/tools/flashy/lib/utils"
	"github.com/facebook/openbmc/tools/flashy/tests"
	"github.com/pkg/errors"
)

func TestFlashFwUtil(t *testing.T) {
	// save log output into buf for testing
	var buf bytes.Buffer
	log.SetOutput(&buf)
	// mock and defer restore runFwUtilCmd
	runFwUtilCmdOrig := runFwUtilCmd
	defer func() {
		log.SetOutput(os.Stderr)
		runFwUtilCmd = runFwUtilCmdOrig
	}()

	exampleStepParams := step.StepParams{
		ImageFilePath: "/tmp/image",
		DeviceID:      "mtd:flash0",
	}

	cases := []struct {
		name            string
		runFwUtilCmdErr error
		want            step.StepExitError
		logContainsSeq  []string
	}{
		{
			name:            "basic successful flash",
			runFwUtilCmdErr: nil,
			want:            nil,
			logContainsSeq: []string{
				"Flashing using fw-util method",
				"Attempting to flash with image file '/tmp/image'",
			},
		},
		{
			name:            "fw-util failed",
			runFwUtilCmdErr: errors.Errorf("RunCommand error"),
			want: step.ExitSafeToReboot{
				errors.Errorf("RunCommand error"),
			},
			logContainsSeq: []string{
				"Flashing using fw-util method",
				"Attempting to flash with image file '/tmp/image'",
			},
		},
	}

	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			buf = bytes.Buffer{}
			runFwUtilCmd = func(imageFilePath string) error {
				if imageFilePath != "/tmp/image" {
					t.Errorf("imageFilePath: want '%v' got '%v'", "/tmp/image", imageFilePath)
				}
				return tc.runFwUtilCmdErr
			}
			got := FlashFwUtil(exampleStepParams)

			step.CompareTestExitErrors(tc.want, got, t)
			tests.LogContainsSeqTest(buf.String(), tc.logContainsSeq, t)
		})
	}
}

func TestRunFwUtilCmd(t *testing.T) {
	// mock and defer restore RunCommand
	runCommandOrig := utils.RunCommand
	defer func() {
		utils.RunCommand = runCommandOrig
	}()

	type cmdRetType struct {
		exitCode int
		err      error
		stdout   string
		stderr   string
	}

	cases := []struct {
		name   string
		cmdRet cmdRetType
		want   error
	}{
		{
			name: "basic succeeding",
			cmdRet: cmdRetType{
				0, nil, "", "",
			},
			want: nil,
		},
		{
			name: "cmd failed",
			cmdRet: cmdRetType{
				1,
				errors.Errorf("fw-util failed"),
				"failed (stdout)",
				"failed (stderr)",
			},
			want: errors.Errorf(
				"Flashing failed with exit code %v, error: %v, stdout: '', stderr: ''",
				1, "fw-util failed",
			),
		},
	}

	wantCmd := "fw-util bmc --update bmc a"
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			utils.RunCommand = func(cmdArr []string, timeoutInSeconds int) (int, error, string, string) {
				cmdStr := strings.Join(cmdArr, " ")
				if cmdStr != wantCmd {
					t.Errorf("cmd: want '%v' got '%v'", wantCmd, cmdStr)
				}
				r := tc.cmdRet
				return r.exitCode, r.err, "", ""
			}

			got := runFwUtilCmd("a")
			tests.CompareTestErrors(tc.want, got, t)
		})
	}
}
