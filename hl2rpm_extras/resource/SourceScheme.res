// HL2RPM: GameUI's dialogs (options, load, save, new game, message boxes, the console) in the
// colours of the main menu (hl2rpm_mainmenu.cpp): dark grey, almost opaque, grey selection
// instead of the orange one. Everything else comes from the stock scheme.
// (copy: hl2rpm_extras/resource/SourceScheme.res)
#base "SourceSchemeBase.res"

Scheme
{
	BaseSettings
	{
		Border.Bright					"92 92 92 255"
		Border.Dark						"14 14 14 255"
		Border.Selection				"0 0 0 255"

		Button.ArmedBgColor				"72 72 72 255"
		Button.DepressedBgColor			"92 92 92 255"

		CheckButton.BgColor				"12 12 12 200"

		Frame.BgColor					"22 22 22 252"
		Frame.OutOfFocusBgColor			"22 22 22 232"
		Frame.TransitionEffectTime		"0.2"

		ListPanel.BgColor				"10 10 10 160"
		ListPanel.SelectedTextColor		"White"
		ListPanel.SelectedBgColor		"92 92 92 255"
		ListPanel.SelectedOutOfFocusBgColor	"70 70 70 255"

		Menu.BgColor					"30 30 30 250"
		Menu.ArmedTextColor				"White"
		Menu.ArmedBgColor				"92 92 92 255"

		ProgressBar.FgColor				"205 205 205 255"
		ProgressBar.BgColor				"10 10 10 160"

		RichText.BgColor				"10 10 10 160"
		RichText.SelectedTextColor		"White"
		RichText.SelectedBgColor		"92 92 92 255"

		ScrollBarSlider.BgColor			"255 255 255 40"

		SectionedListPanel.BgColor		"10 10 10 160"
		SectionedListPanel.SelectedTextColor			"White"
		SectionedListPanel.SelectedBgColor				"92 92 92 255"
		SectionedListPanel.OutOfFocusSelectedTextColor	"White"
		SectionedListPanel.OutOfFocusSelectedBgColor	"70 70 70 255"

		TextEntry.BgColor				"10 10 10 170"
		TextEntry.SelectedTextColor		"White"
		TextEntry.SelectedBgColor		"92 92 92 255"
		TextEntry.OutOfFocusSelectedBgColor	"70 70 70 255"

		Tooltip.TextColor				"225 225 225 255"
		Tooltip.BgColor					"52 52 52 250"

		TreeView.BgColor				"10 10 10 160"

		MainMenu.Backdrop				"0 0 0 110"

		NewGame.SelectionColor			"150 150 150 255"
	}
}
