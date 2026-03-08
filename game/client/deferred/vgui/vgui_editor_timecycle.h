#ifndef C_VGUI_EDITOR_TIMECYCLE_H
#define C_VGUI_EDITOR_TIMECYCLE_H


#include "vgui_controls/Frame.h"

namespace vgui
{
	class ComboBox;
	class Label;
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

	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );

private:

	void PopulateCombos();
	void RefreshFromTimecycle();
	void RefreshCurrentTimeDisplay();
	void ApplyTimeOfDay();
	void ApplyTimeScale();
	void SelectItemByValue( vgui::ComboBox *pCombo, const char *pszValue );

	bool m_bRefreshing;
	int m_iLastDisplayedHour;
	int m_iLastDisplayedMinute;

	vgui::Label *m_pLabelCurrentTimeTitle;
	vgui::Label *m_pLabelCurrentTimeValue;
	vgui::Label *m_pLabelTimeOfDay;
	vgui::Label *m_pLabelTimeScale;
	vgui::Label *m_pLabelWeatherPreset;

	vgui::ComboBox *m_pComboTimeOfDay;
	vgui::ComboBox *m_pComboTimeScale;
	vgui::ComboBox *m_pComboWeatherPreset;
};


#endif
