#ifndef C_VGUI_EDITOR_TIMECYCLE_H
#define C_VGUI_EDITOR_TIMECYCLE_H


#include "vgui_controls/Frame.h"

namespace vgui
{
	class ComboBox;
	class Label;
	class CheckButton;
	class Button;
}


class CVGUILightEditor_Timecycle : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CVGUILightEditor_Timecycle, vgui::Frame );

public:

	CVGUILightEditor_Timecycle( vgui::Panel *pParent );
	~CVGUILightEditor_Timecycle();

	void OpenEditor();

protected:

	void OnThink();
	void PerformLayout();
	void OnCommand( const char *pszCommand );

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel );

private:

	void PopulateCombos();
	void RefreshFromTimecycle();
	void RefreshCurrentTimeDisplay();
	void RefreshWeatherDisplay();
	void ApplyTimeOfDay();
	void ApplyTimeScale();
	void ApplyWeather();
	void SelectItemByValue( vgui::ComboBox *pCombo, const char *pszValue );

	bool m_bRefreshing;
	int m_iLastDisplayedHour;
	int m_iLastDisplayedMinute;
	int m_iLastWeatherTarget;
	int m_iLastWeatherPercent;

	vgui::Label *m_pLabelCurrentTimeTitle;
	vgui::Label *m_pLabelCurrentTimeValue;
	vgui::Label *m_pLabelTimeOfDay;
	vgui::Label *m_pLabelTimeScale;
	vgui::Label *m_pLabelWeatherState;
	vgui::Label *m_pLabelWeatherPreset;
	vgui::Label *m_pLabelWeatherTransition;

	vgui::ComboBox *m_pComboTimeOfDay;
	vgui::ComboBox *m_pComboTimeScale;
	vgui::ComboBox *m_pComboWeatherPreset;
	vgui::ComboBox *m_pComboWeatherTransition;

	vgui::CheckButton *m_pCheckAutoWeather;
	vgui::Button *m_pButtonNextWeather;
	vgui::Button *m_pButtonLightning;
};


#endif
