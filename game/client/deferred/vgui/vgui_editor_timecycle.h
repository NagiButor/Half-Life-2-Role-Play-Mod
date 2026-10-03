#ifndef C_VGUI_EDITOR_TIMECYCLE_H
#define C_VGUI_EDITOR_TIMECYCLE_H


#include "vgui_controls/Frame.h"

namespace vgui
{
	class ComboBox;
	class Label;
	class CheckButton;
	class Button;
	class Slider;
}


// HL2RPM: "Time of day and weather" window. F1 opens it on its own (the old
// light editor with its own lighting stays available through
// r_deferred_light_editor_toggle); changes apply to the world immediately.
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
	void OnKeyCodeTyped( vgui::KeyCode code );
	void OnClose();

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnCheckButtonChecked, "CheckButtonChecked", panel );
	MESSAGE_FUNC_PARAMS( OnSliderMoved, "SliderMoved", data );
	MESSAGE_FUNC_PARAMS( OnSliderDragStart, "SliderDragStart", data );
	MESSAGE_FUNC_PARAMS( OnSliderDragEnd, "SliderDragEnd", data );

private:

	void PopulateCombos();
	void RefreshFromTimecycle();
	void RefreshCurrentTimeDisplay();
	void RefreshWeatherDisplay();
	void ApplyTimeOfDay( int iQuarterHours );
	void ApplyTimeScale();
	void ApplyWeather();
	void SetSliderTimeLabel( int iQuarterHours );
	void SelectItemByValue( vgui::ComboBox *pCombo, const char *pszValue );

	bool m_bRefreshing;
	bool m_bDraggingTime;
	float m_flNextTimeApply;
	int m_iPendingQuarterHours;
	int m_iLastDisplayedHour;
	int m_iLastDisplayedMinute;
	int m_iLastWeatherTarget;
	int m_iLastWeatherPercent;

	vgui::Label *m_pLabelCurrentTimeTitle;
	vgui::Label *m_pLabelCurrentTimeValue;
	vgui::Label *m_pLabelTimeOfDay;
	vgui::Label *m_pLabelSliderTime;
	vgui::Label *m_pLabelTimeScale;
	vgui::Label *m_pLabelWeatherState;
	vgui::Label *m_pLabelWeatherPreset;
	vgui::Label *m_pLabelWeatherTransition;

	vgui::Slider *m_pSliderTime;
	vgui::ComboBox *m_pComboTimeScale;
	vgui::ComboBox *m_pComboWeatherPreset;
	vgui::ComboBox *m_pComboWeatherTransition;

	vgui::CheckButton *m_pCheckAutoWeather;
	vgui::Button *m_pButtonNextWeather;
	vgui::Button *m_pButtonLightning;
	vgui::Button *m_pButtonClose;
};

// standalone window (F1)
void TimeWeatherPanel_Toggle();
void TimeWeatherPanel_Open();
void TimeWeatherPanel_Close();
void TimeWeatherPanel_Destroy();
bool TimeWeatherPanel_IsVisible();


#endif
