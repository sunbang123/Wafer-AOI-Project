
// WaferUINativeView.h: CWaferUINativeView 클래스의 인터페이스
//

#pragma once

#include <atlimage.h>
#include <vector>


class CWaferUINativeView : public CView
{
protected: // serialization에서만 만들어집니다.
	CWaferUINativeView() noexcept;
	DECLARE_DYNCREATE(CWaferUINativeView)

// 특성입니다.
public:
	CWaferUINativeDoc* GetDocument() const;

// 작업입니다.
public:

// 재정의입니다.
public:
	virtual void OnDraw(CDC* pDC);  // 이 뷰를 그리기 위해 재정의되었습니다.
	virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
	virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
	virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
	virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);

// 구현입니다.
public:
	virtual ~CWaferUINativeView();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// 생성된 메시지 맵 함수
protected:
	afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
	afx_msg void OnSize(UINT nType, int cx, int cy);
	afx_msg void OnRefreshImages();
	afx_msg void OnAnalyzeImage();
	afx_msg void OnImageSelectionChanged();
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	afx_msg void OnFilePrintPreview();
	afx_msg void OnRButtonUp(UINT nFlags, CPoint point);
	afx_msg void OnContextMenu(CWnd* pWnd, CPoint point);
	DECLARE_MESSAGE_MAP()

private:
	using LoadWaferModelFromPathProc = bool(__cdecl*)(const char*);
	using PredictWaferDefectWithConfidenceProc = int(__cdecl*)(const char*, float*);

	void CreateChildControls();
	void LayoutChildControls(int cx, int cy);
	void LoadLabels();
	void LoadTestImages();
	void LoadSelectedImage();
	void SetResultText(const CString& text, COLORREF color);
	bool EnsureVisionEngineLoaded();
	void AddNativeDependencyDirectories() const;
	CString FindExistingDirectory(const std::vector<CString>& candidates) const;
	CString FindExistingFile(const std::vector<CString>& candidates) const;
	CString GetExecutableDirectory() const;
	CString GetProjectRootDirectory() const;
	CString GetLabelText(int classIndex) const;
	CString GetPredictionErrorText(int errorCode) const;
	static bool IsSupportedImagePath(const CString& path);
	static CStringA ToUtf8CStringA(const CString& text);

	CListBox m_imageList;
	CButton m_refreshButton;
	CButton m_analyzeButton;
	CStatic m_titleLabel;
	CStatic m_selectedImageLabel;
	CStatic m_resultLabel;
	CImage m_previewImage;
	CBrush m_backgroundBrush;
	std::vector<CString> m_imagePaths;
	std::vector<CString> m_labels;
	CString m_selectedImagePath;
	CString m_modelPath;
	CString m_dllPath;
	HMODULE m_visionModule = nullptr;
	LoadWaferModelFromPathProc m_loadModel = nullptr;
	PredictWaferDefectWithConfidenceProc m_predict = nullptr;
	bool m_modelLoaded = false;
	COLORREF m_resultColor = RGB(71, 85, 105);
};

#ifndef _DEBUG  // WaferUINativeView.cpp의 디버그 버전
inline CWaferUINativeDoc* CWaferUINativeView::GetDocument() const
   { return reinterpret_cast<CWaferUINativeDoc*>(m_pDocument); }
#endif

