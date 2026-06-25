
// WaferUINativeView.cpp: CWaferUINativeView 클래스의 구현
//

#include "pch.h"
#include "framework.h"
// SHARED_HANDLERS는 미리 보기, 축소판 그림 및 검색 필터 처리기를 구현하는 ATL 프로젝트에서 정의할 수 있으며
// 해당 프로젝트와 문서 코드를 공유하도록 해 줍니다.
#ifndef SHARED_HANDLERS
#include "WaferUINative.h"
#endif

#include "WaferUINativeDoc.h"
#include "WaferUINativeView.h"

#include <algorithm>
#include <memory>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	constexpr UINT_PTR IdImageList = 1001;
	constexpr UINT_PTR IdRefreshButton = 1002;
	constexpr UINT_PTR IdAnalyzeButton = 1003;
	constexpr UINT_PTR IdTitleLabel = 1004;
	constexpr UINT_PTR IdSelectedImageLabel = 1005;
	constexpr UINT_PTR IdResultLabel = 1006;
	constexpr int PanelPadding = 18;
	constexpr int LeftPanelWidth = 240;
	constexpr int PanelGap = 18;

	int MaxInt(int left, int right)
	{
		return left > right ? left : right;
	}

	double MinDouble(double left, double right)
	{
		return left < right ? left : right;
	}
}

// CWaferUINativeView

IMPLEMENT_DYNCREATE(CWaferUINativeView, CView)

BEGIN_MESSAGE_MAP(CWaferUINativeView, CView)
	// 표준 인쇄 명령입니다.
	ON_COMMAND(ID_FILE_PRINT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, &CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, &CWaferUINativeView::OnFilePrintPreview)
	ON_WM_CONTEXTMENU()
	ON_WM_RBUTTONUP()
	ON_WM_CREATE()
	ON_WM_SIZE()
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IdRefreshButton, &CWaferUINativeView::OnRefreshImages)
	ON_BN_CLICKED(IdAnalyzeButton, &CWaferUINativeView::OnAnalyzeImage)
	ON_LBN_SELCHANGE(IdImageList, &CWaferUINativeView::OnImageSelectionChanged)
END_MESSAGE_MAP()

// CWaferUINativeView 생성/소멸

CWaferUINativeView::CWaferUINativeView() noexcept
{
	m_backgroundBrush.CreateSolidBrush(RGB(245, 247, 250));
}

CWaferUINativeView::~CWaferUINativeView()
{
	if (!m_previewImage.IsNull())
	{
		m_previewImage.Destroy();
	}

	if (m_visionModule != nullptr)
	{
		FreeLibrary(m_visionModule);
		m_visionModule = nullptr;
	}
}

BOOL CWaferUINativeView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: CREATESTRUCT cs를 수정하여 여기에서
	//  Window 클래스 또는 스타일을 수정합니다.

	return CView::PreCreateWindow(cs);
}

// CWaferUINativeView 그리기

void CWaferUINativeView::OnDraw(CDC* pDC)
{
	CWaferUINativeDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	if (!pDoc)
		return;

	CRect clientRect;
	GetClientRect(&clientRect);
	pDC->FillSolidRect(clientRect, RGB(245, 247, 250));

	CRect imageFrame(
		PanelPadding + LeftPanelWidth + PanelGap + PanelPadding,
		PanelPadding,
		clientRect.right - PanelPadding,
		MaxInt(PanelPadding + 260, clientRect.bottom - 112));
	pDC->FillSolidRect(imageFrame, RGB(17, 24, 39));

	if (m_previewImage.IsNull())
	{
		CString hint = _T("Select a wafer image.");
		pDC->SetBkMode(TRANSPARENT);
		pDC->SetTextColor(RGB(226, 232, 240));
		pDC->DrawText(hint, imageFrame, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
		return;
	}

	CRect drawRect = imageFrame;
	drawRect.DeflateRect(14, 14);

	const int imageWidth = m_previewImage.GetWidth();
	const int imageHeight = m_previewImage.GetHeight();
	if (imageWidth <= 0 || imageHeight <= 0)
	{
		return;
	}

	const double scale = MinDouble(
		static_cast<double>(drawRect.Width()) / static_cast<double>(imageWidth),
		static_cast<double>(drawRect.Height()) / static_cast<double>(imageHeight));
	const int scaledWidth = static_cast<int>(imageWidth * scale);
	const int scaledHeight = static_cast<int>(imageHeight * scale);
	CRect target(
		drawRect.left + (drawRect.Width() - scaledWidth) / 2,
		drawRect.top + (drawRect.Height() - scaledHeight) / 2,
		drawRect.left + (drawRect.Width() + scaledWidth) / 2,
		drawRect.top + (drawRect.Height() + scaledHeight) / 2);

	m_previewImage.Draw(pDC->GetSafeHdc(), target);
}


// CWaferUINativeView 인쇄


void CWaferUINativeView::OnFilePrintPreview()
{
#ifndef SHARED_HANDLERS
	AFXPrintPreview(this);
#endif
}

BOOL CWaferUINativeView::OnPreparePrinting(CPrintInfo* pInfo)
{
	// 기본적인 준비
	return DoPreparePrinting(pInfo);
}

void CWaferUINativeView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: 인쇄하기 전에 추가 초기화 작업을 추가합니다.
}

void CWaferUINativeView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: 인쇄 후 정리 작업을 추가합니다.
}

void CWaferUINativeView::OnRButtonUp(UINT /* nFlags */, CPoint point)
{
	ClientToScreen(&point);
	OnContextMenu(this, point);
}

void CWaferUINativeView::OnContextMenu(CWnd* /* pWnd */, CPoint point)
{
#ifndef SHARED_HANDLERS
	theApp.GetContextMenuManager()->ShowPopupMenu(IDR_POPUP_EDIT, point.x, point.y, this, TRUE);
#endif
}


// CWaferUINativeView 진단

#ifdef _DEBUG
void CWaferUINativeView::AssertValid() const
{
	CView::AssertValid();
}

void CWaferUINativeView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CWaferUINativeDoc* CWaferUINativeView::GetDocument() const // 디버그되지 않은 버전은 인라인으로 지정됩니다.
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CWaferUINativeDoc)));
	return (CWaferUINativeDoc*)m_pDocument;
}
#endif //_DEBUG


// CWaferUINativeView 메시지 처리기

int CWaferUINativeView::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	if (CView::OnCreate(lpCreateStruct) == -1)
	{
		return -1;
	}

	CreateChildControls();
	LoadLabels();
	EnsureVisionEngineLoaded();
	LoadTestImages();
	return 0;
}

void CWaferUINativeView::OnSize(UINT nType, int cx, int cy)
{
	CView::OnSize(nType, cx, cy);
	LayoutChildControls(cx, cy);
	Invalidate();
}

void CWaferUINativeView::CreateChildControls()
{
	m_titleLabel.Create(_T("TestImages"), WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(), this, IdTitleLabel);
	m_imageList.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, CRect(), this, IdImageList);
	m_refreshButton.Create(_T("Refresh"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, IdRefreshButton);
	m_selectedImageLabel.Create(_T("No image selected"), WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(), this, IdSelectedImageLabel);
	m_resultLabel.Create(_T("Loading model..."), WS_CHILD | WS_VISIBLE | SS_LEFT, CRect(), this, IdResultLabel);
	m_analyzeButton.Create(_T("Analyze"), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, CRect(), this, IdAnalyzeButton);
	m_analyzeButton.EnableWindow(FALSE);

	CFont* font = GetFont();
	m_titleLabel.SetFont(font);
	m_imageList.SetFont(font);
	m_refreshButton.SetFont(font);
	m_selectedImageLabel.SetFont(font);
	m_resultLabel.SetFont(font);
	m_analyzeButton.SetFont(font);
}

void CWaferUINativeView::LayoutChildControls(int cx, int cy)
{
	if (!::IsWindow(m_imageList.GetSafeHwnd()))
	{
		return;
	}

	const int left = PanelPadding;
	const int top = PanelPadding;
	const int bottom = MaxInt(top + 180, cy - PanelPadding);

	m_titleLabel.MoveWindow(left + 14, top + 14, LeftPanelWidth - 28, 28);
	m_refreshButton.MoveWindow(left + 14, bottom - 48, LeftPanelWidth - 28, 34);
	m_imageList.MoveWindow(left + 14, top + 54, LeftPanelWidth - 28, MaxInt(80, bottom - top - 116));

	const int rightLeft = left + LeftPanelWidth + PanelGap;
	const int rightWidth = MaxInt(260, cx - rightLeft - PanelPadding);
	m_selectedImageLabel.MoveWindow(rightLeft + 18, bottom - 78, MaxInt(120, rightWidth - 180), 24);
	m_resultLabel.MoveWindow(rightLeft + 18, bottom - 48, MaxInt(120, rightWidth - 180), 24);
	m_analyzeButton.MoveWindow(rightLeft + rightWidth - 150, bottom - 62, 132, 42);
}

void CWaferUINativeView::OnRefreshImages()
{
	LoadTestImages();
}

void CWaferUINativeView::OnAnalyzeImage()
{
	if (m_selectedImagePath.IsEmpty())
	{
		SetResultText(_T("Select an image first."), RGB(190, 18, 60));
		return;
	}

	if (!EnsureVisionEngineLoaded())
	{
		SetResultText(_T("CoreVision.dll or model could not be loaded."), RGB(190, 18, 60));
		return;
	}

	CStringA imagePathUtf8 = ToUtf8CStringA(m_selectedImagePath);
	float confidence = 0.0f;
	const int classIndex = m_predict(imagePathUtf8, &confidence);
	if (classIndex < 0)
	{
		SetResultText(GetPredictionErrorText(classIndex), RGB(190, 18, 60));
		return;
	}

	const CString label = GetLabelText(classIndex);
	const bool isDefect = label.CompareNoCase(_T("none")) != 0;
	CString result;
	result.Format(_T("Prediction: %s / Class: %s / Confidence: %.1f%%"),
		isDefect ? _T("Defect") : _T("Normal"),
		static_cast<LPCTSTR>(label),
		confidence * 100.0f);
	SetResultText(result, isDefect ? RGB(190, 18, 60) : RGB(22, 101, 52));
}

void CWaferUINativeView::OnImageSelectionChanged()
{
	const int selectedIndex = m_imageList.GetCurSel();
	if (selectedIndex == LB_ERR || selectedIndex >= static_cast<int>(m_imagePaths.size()))
	{
		return;
	}

	m_selectedImagePath = m_imagePaths[selectedIndex];
	LoadSelectedImage();
}

HBRUSH CWaferUINativeView::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH brush = CView::OnCtlColor(pDC, pWnd, nCtlColor);
	const bool isStaticText = nCtlColor == CTLCOLOR_STATIC;

	if (isStaticText)
	{
		pDC->SetBkMode(OPAQUE);
		pDC->SetBkColor(RGB(245, 247, 250));
	}

	if (pWnd != nullptr && pWnd->GetSafeHwnd() == m_resultLabel.GetSafeHwnd())
	{
		pDC->SetTextColor(m_resultColor);
		return static_cast<HBRUSH>(m_backgroundBrush.GetSafeHandle());
	}

	if (isStaticText)
	{
		return static_cast<HBRUSH>(m_backgroundBrush.GetSafeHandle());
	}

	return brush;
}

void CWaferUINativeView::LoadLabels()
{
	m_labels.clear();
	const CString root = GetProjectRootDirectory();
	const CString labelsPath = FindExistingFile({
		root + _T("\\AI_Model\\wafer_labels.json"),
		GetExecutableDirectory() + _T("\\AI_Model\\wafer_labels.json"),
		GetExecutableDirectory() + _T("\\..\\..\\AI_Model\\wafer_labels.json")
		});

	CStdioFile file;
	if (!file.Open(labelsPath, CFile::modeRead | CFile::typeText))
	{
		m_labels = { _T("none"), _T("Center"), _T("Donut"), _T("Edge-Loc"), _T("Edge-Ring"), _T("Loc"), _T("Near-full"), _T("Random"), _T("Scratch") };
		return;
	}

	CString line;
	while (file.ReadString(line))
	{
		line.Trim();
		if (line.GetLength() < 2 || line[0] != _T('"'))
		{
			continue;
		}

		const int endQuote = line.Find(_T('"'), 1);
		if (endQuote > 1)
		{
			m_labels.push_back(line.Mid(1, endQuote - 1));
		}
	}
}

void CWaferUINativeView::LoadTestImages()
{
	m_imageList.ResetContent();
	m_imagePaths.clear();
	m_selectedImagePath.Empty();

	const CString root = GetProjectRootDirectory();
	const CString imageDirectory = FindExistingDirectory({
		GetExecutableDirectory() + _T("\\TestImages"),
		root + _T("\\Assets\\TestImages"),
		root + _T("\\WaferUI\\TestImages")
		});

	if (imageDirectory.IsEmpty())
	{
		SetResultText(_T("No TestImages folder was found."), RGB(190, 18, 60));
		m_analyzeButton.EnableWindow(FALSE);
		Invalidate();
		return;
	}

	CFileFind finder;
	BOOL working = finder.FindFile(imageDirectory + _T("\\*.*"));
	while (working)
	{
		working = finder.FindNextFile();
		if (finder.IsDots() || finder.IsDirectory())
		{
			continue;
		}

		const CString path = finder.GetFilePath();
		if (IsSupportedImagePath(path))
		{
			m_imagePaths.push_back(path);
		}
	}

	std::sort(m_imagePaths.begin(), m_imagePaths.end(), [](const CString& left, const CString& right) {
		return left.CompareNoCase(right) < 0;
		});

	for (const CString& path : m_imagePaths)
	{
		const int slash = path.ReverseFind(_T('\\'));
		m_imageList.AddString(slash >= 0 ? path.Mid(slash + 1) : path);
	}

	if (!m_imagePaths.empty())
	{
		m_imageList.SetCurSel(0);
		m_selectedImagePath = m_imagePaths[0];
		LoadSelectedImage();
		m_analyzeButton.EnableWindow(m_modelLoaded ? TRUE : FALSE);
	}
	else
	{
		SetResultText(_T("Add PNG/JPG/BMP images to TestImages."), RGB(71, 85, 105));
		m_selectedImageLabel.SetWindowText(_T("No images found"));
		m_analyzeButton.EnableWindow(FALSE);
		Invalidate();
	}
}

void CWaferUINativeView::LoadSelectedImage()
{
	if (!m_previewImage.IsNull())
	{
		m_previewImage.Destroy();
	}

	HRESULT hr = m_previewImage.Load(m_selectedImagePath);
	const int slash = m_selectedImagePath.ReverseFind(_T('\\'));
	m_selectedImageLabel.SetWindowText(slash >= 0 ? m_selectedImagePath.Mid(slash + 1) : m_selectedImagePath);

	if (FAILED(hr))
	{
		SetResultText(_T("Image preview failed."), RGB(190, 18, 60));
	}
	else
	{
		SetResultText(m_modelLoaded ? _T("Ready to analyze.") : _T("Model is not loaded."), m_modelLoaded ? RGB(71, 85, 105) : RGB(190, 18, 60));
	}

	Invalidate();
}

void CWaferUINativeView::SetResultText(const CString& text, COLORREF color)
{
	m_resultColor = color;
	if (::IsWindow(m_resultLabel.GetSafeHwnd()))
	{
		m_resultLabel.SetWindowText(text);
		m_resultLabel.Invalidate();
	}
}

bool CWaferUINativeView::EnsureVisionEngineLoaded()
{
	if (m_modelLoaded && m_predict != nullptr)
	{
		return true;
	}

	const CString exeDirectory = GetExecutableDirectory();
	const CString root = GetProjectRootDirectory();
	m_dllPath = FindExistingFile({
		exeDirectory + _T("\\CoreVision.dll"),
		root + _T("\\x64\\Debug\\CoreVision.dll"),
		root + _T("\\x64\\Release\\CoreVision.dll")
		});
	m_modelPath = FindExistingFile({
		exeDirectory + _T("\\AI_Model\\wafer_defect_model.onnx"),
		root + _T("\\AI_Model\\wafer_defect_model.onnx"),
		exeDirectory + _T("\\..\\..\\AI_Model\\wafer_defect_model.onnx")
		});

	if (m_dllPath.IsEmpty() || m_modelPath.IsEmpty())
	{
		m_modelLoaded = false;
		SetResultText(_T("CoreVision.dll or ONNX model was not found."), RGB(190, 18, 60));
		return false;
	}

	if (m_visionModule == nullptr)
	{
		AddNativeDependencyDirectories();
		m_visionModule = LoadLibrary(m_dllPath);
	}

	if (m_visionModule == nullptr)
	{
		m_modelLoaded = false;
		SetResultText(_T("Failed to load CoreVision.dll."), RGB(190, 18, 60));
		return false;
	}

	m_loadModel = reinterpret_cast<LoadWaferModelFromPathProc>(GetProcAddress(m_visionModule, "LoadWaferModelFromPath"));
	m_predict = reinterpret_cast<PredictWaferDefectWithConfidenceProc>(GetProcAddress(m_visionModule, "PredictWaferDefectWithConfidence"));
	if (m_loadModel == nullptr || m_predict == nullptr)
	{
		m_modelLoaded = false;
		SetResultText(_T("CoreVision.dll exports are missing. Rebuild CoreVision."), RGB(190, 18, 60));
		return false;
	}

	CStringA modelPathUtf8 = ToUtf8CStringA(m_modelPath);
	m_modelLoaded = m_loadModel(modelPathUtf8);
	SetResultText(m_modelLoaded ? _T("Model loaded.") : _T("Model load failed."), m_modelLoaded ? RGB(71, 85, 105) : RGB(190, 18, 60));
	return m_modelLoaded;
}

void CWaferUINativeView::AddNativeDependencyDirectories() const
{
	TCHAR opencvDirectory[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariable(_T("OPENCV_DIR"), opencvDirectory, MAX_PATH);
	if (length > 0 && length < MAX_PATH)
	{
		const CString opencvBinDirectory = CString(opencvDirectory) + _T("\\x64\\vc17\\bin");
		if (GetFileAttributes(opencvBinDirectory) != INVALID_FILE_ATTRIBUTES)
		{
			SetDllDirectory(opencvBinDirectory);
		}
	}
}

CString CWaferUINativeView::FindExistingDirectory(const std::vector<CString>& candidates) const
{
	for (CString path : candidates)
	{
		TCHAR fullPath[MAX_PATH] = {};
		if (GetFullPathName(path, MAX_PATH, fullPath, nullptr) > 0 && GetFileAttributes(fullPath) != INVALID_FILE_ATTRIBUTES)
		{
			if ((GetFileAttributes(fullPath) & FILE_ATTRIBUTE_DIRECTORY) != 0)
			{
				return fullPath;
			}
		}
	}

	return CString();
}

CString CWaferUINativeView::FindExistingFile(const std::vector<CString>& candidates) const
{
	for (CString path : candidates)
	{
		TCHAR fullPath[MAX_PATH] = {};
		if (GetFullPathName(path, MAX_PATH, fullPath, nullptr) > 0 && GetFileAttributes(fullPath) != INVALID_FILE_ATTRIBUTES)
		{
			if ((GetFileAttributes(fullPath) & FILE_ATTRIBUTE_DIRECTORY) == 0)
			{
				return fullPath;
			}
		}
	}

	return CString();
}

CString CWaferUINativeView::GetExecutableDirectory() const
{
	TCHAR path[MAX_PATH] = {};
	GetModuleFileName(nullptr, path, MAX_PATH);
	CString directory(path);
	const int slash = directory.ReverseFind(_T('\\'));
	return slash >= 0 ? directory.Left(slash) : directory;
}

CString CWaferUINativeView::GetProjectRootDirectory() const
{
	CString directory = GetExecutableDirectory();
	for (int i = 0; i < 5; ++i)
	{
		if (GetFileAttributes(directory + _T("\\AI_Model")) != INVALID_FILE_ATTRIBUTES)
		{
			return directory;
		}

		const int slash = directory.ReverseFind(_T('\\'));
		if (slash <= 2)
		{
			break;
		}
		directory = directory.Left(slash);
	}

	return GetExecutableDirectory();
}

CString CWaferUINativeView::GetLabelText(int classIndex) const
{
	if (classIndex >= 0 && classIndex < static_cast<int>(m_labels.size()))
	{
		return m_labels[classIndex];
	}

	CString fallback;
	fallback.Format(_T("Class %d"), classIndex);
	return fallback;
}

CString CWaferUINativeView::GetPredictionErrorText(int errorCode) const
{
	switch (errorCode)
	{
	case -1:
		return _T("Image path is empty.");
	case -2:
		return _T("Model load failed.");
	case -3:
		return _T("Image read failed.");
	case -4:
		return _T("OpenCV inference failed.");
	case -5:
		return _T("Unknown inference error.");
	default:
	{
		CString message;
		message.Format(_T("Inference failed. Code: %d"), errorCode);
		return message;
	}
	}
}

bool CWaferUINativeView::IsSupportedImagePath(const CString& path)
{
	const int dot = path.ReverseFind(_T('.'));
	if (dot < 0)
	{
		return false;
	}

	CString extension = path.Mid(dot);
	extension.MakeLower();
	return extension == _T(".png") || extension == _T(".jpg") || extension == _T(".jpeg") || extension == _T(".bmp");
}

CStringA CWaferUINativeView::ToUtf8CStringA(const CString& text)
{
	CT2A converted(text, CP_UTF8);
	return CStringA(converted);
}
