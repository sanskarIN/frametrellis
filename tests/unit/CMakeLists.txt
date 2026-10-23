# SPDX-License-Identifier: GPL-3.0-or-later
ft_add_test(tst_folder_import SOURCES tst_folder_import.cpp LIBS ft_media LABELS unit)
ft_add_test(tst_time SOURCES tst_time.cpp LIBS ft_core LABELS unit)
ft_add_test(tst_timeline SOURCES tst_timeline.cpp LIBS ft_timeline LABELS unit)
ft_add_test(tst_project_format SOURCES tst_project_format.cpp LIBS ft_project LABELS unit)
ft_add_test(tst_editing SOURCES tst_editing.cpp LIBS ft_editing LABELS unit)
ft_add_test(tst_rendering SOURCES tst_rendering.cpp LIBS ft_rendering LABELS unit)
