// Copyright © 2022 Favro Holding AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

namespace NMib::NGit
{
	template <typename tf_CStr>
	void CGitHostingProvider::CUser::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("User: {}") << m_Login;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CApp::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("App: {}") << m_Slug;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CTeam::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Team: {}") << m_Slug;
	}

	template <typename tf_CStr>
	void CGitHostingProvider::CRequiredStatusCheck::f_Format(tf_CStr &o_Str) const
	{
		o_Str += typename tf_CStr::CFormat("Context: {} App: {}") << m_Context << m_App;
	}
}
