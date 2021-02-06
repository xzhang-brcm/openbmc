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

package install

import (
	"os"
	"path/filepath"
	"testing"

	"github.com/facebook/openbmc/tools/flashy/lib/fileutils"
	"github.com/facebook/openbmc/tools/flashy/lib/step"
	"github.com/facebook/openbmc/tools/flashy/lib/utils"
	"github.com/facebook/openbmc/tools/flashy/tests"
)

func init() {
	populateStepPaths()
}

// step paths for each test, e.g.
// "checks_and_remediations/common/00_truncate_logs"
// populated by populateStepPaths based on StepMap keys
var stepPaths []string

// directories to check
// All non-`_test.go` .go files will be checked in these directories
// to make sure the step is registered correctly
var directoriesToTest = []string{
	"checks_and_remediations",
	"flash_procedure",
}

func populateStepPaths() {
	for path := range step.StepMap {
		stepPaths = append(stepPaths, path)
	}
}

// TestStepMap checks that all required files are registered correctly
// in StepMap.
func TestStepMap(t *testing.T) {
	for _, dir := range directoriesToTest {
		absDirPath := filepath.Join(fileutils.SourceRootDir, dir)
		err := filepath.Walk(absDirPath,
			func(path string, info os.FileInfo, err error) error {
				if err != nil {
					return err
				}

				if info.IsDir() || // skip directories
					filepath.Ext(info.Name()) != ".go" || // skip non .go files
					tests.IsGoTestFileName(info.Name()) { // skip _test.go files
					return nil
				}

				symlinkPath := fileutils.GetSymlinkPathForSourceFile(path)

				if utils.StringFind(symlinkPath, stepPaths) < 0 {
					stepGuide := `Please register the step using RegisterStep in init().
Check also that the package is imported in install.go.`

					t.Errorf("'%v' not registered!\n%v",
						symlinkPath, stepGuide)
				}

				return nil
			})
		if err != nil {
			t.Errorf("TestStepMap failed: %v", err)
		}
	}
}
