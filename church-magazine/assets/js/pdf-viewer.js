( function () {
	'use strict';

	document.addEventListener( 'DOMContentLoaded', function () {
		var layouts = document.querySelectorAll( '.cm-pdf-layout' );
		if ( ! layouts.length || typeof window.pdfjsLib === 'undefined' ) {
			return;
		}

		if ( window.ChurchMagazinePdf && window.ChurchMagazinePdf.workerSrc ) {
			window.pdfjsLib.GlobalWorkerOptions.workerSrc = window.ChurchMagazinePdf.workerSrc;
		}

		layouts.forEach( initViewer );
	} );

	function initViewer( layout ) {
		var url = layout.getAttribute( 'data-pdf-url' );
		if ( ! url ) {
			return;
		}

		var canvas     = layout.querySelector( '.cm-pdf-canvas' );
		var loading    = layout.querySelector( '.cm-pdf-loading' );
		var canvasWrap = layout.querySelector( '.cm-pdf-canvas-wrap' );
		var pageInput  = layout.querySelector( '.cm-pdf-page-input' );
		var pageCount  = layout.querySelector( '.cm-pdf-page-count' );
		var prevBtn    = layout.querySelector( '.cm-pdf-prev' );
		var nextBtn    = layout.querySelector( '.cm-pdf-next' );
		var zoomInBtn  = layout.querySelector( '.cm-pdf-zoom-in' );
		var zoomOutBtn = layout.querySelector( '.cm-pdf-zoom-out' );
		var tocLinks   = layout.querySelectorAll( '.cm-toc-page-link' );
		var tocItems   = layout.querySelectorAll( '.cm-toc-item' );
		var sidebar    = layout.querySelector( '.cm-sidebar' );
		var toggle     = layout.querySelector( '.cm-toc-toggle' );

		var ctx           = canvas.getContext( '2d' );
		var pdfDoc        = null;
		var currentPage   = 1;
		var totalPages    = 1;
		var zoomFactor    = 1;
		var pageRendering = false;
		var pendingPage   = null;
		var baseWidth     = null;

		if ( toggle && sidebar ) {
			toggle.addEventListener( 'click', function () {
				var isOpen = sidebar.classList.toggle( 'is-open' );
				toggle.setAttribute( 'aria-expanded', isOpen ? 'true' : 'false' );
			} );
		}

		window.pdfjsLib.getDocument( url ).promise.then( function ( doc ) {
			pdfDoc = doc;
			totalPages = doc.numPages;
			pageCount.textContent = totalPages;
			pageInput.setAttribute( 'max', totalPages );
			if ( loading ) {
				loading.style.display = 'none';
			}
			renderPage( currentPage );
		} ).catch( function () {
			if ( loading ) {
				loading.textContent = 'Unable to load the PDF.';
			}
		} );

		function renderPage( num ) {
			num = Math.min( Math.max( 1, num ), totalPages );

			if ( pageRendering ) {
				pendingPage = num;
				return;
			}

			pageRendering = true;

			pdfDoc.getPage( num ).then( function ( page ) {
				if ( null === baseWidth ) {
					var naturalViewport = page.getViewport( { scale: 1 } );
					baseWidth = canvasWrap.clientWidth / naturalViewport.width;
				}

				var viewport = page.getViewport( { scale: baseWidth * zoomFactor } );
				canvas.width  = viewport.width;
				canvas.height = viewport.height;

				var renderTask = page.render( { canvasContext: ctx, viewport: viewport } );

				renderTask.promise.then( function () {
					pageRendering = false;
					currentPage = num;
					pageInput.value = num;
					updateActiveTocItem();
					updateNavButtons();

					if ( null !== pendingPage ) {
						var next = pendingPage;
						pendingPage = null;
						renderPage( next );
					}
				} );
			} );
		}

		function updateNavButtons() {
			prevBtn.disabled = currentPage <= 1;
			nextBtn.disabled = currentPage >= totalPages;
		}

		function updateActiveTocItem() {
			var activeItem = null;

			tocLinks.forEach( function ( link ) {
				var page = parseInt( link.getAttribute( 'data-page' ), 10 );
				if ( page <= currentPage ) {
					activeItem = link.closest( '.cm-toc-item' );
				}
			} );

			tocItems.forEach( function ( item ) {
				item.classList.remove( 'is-active' );
			} );

			if ( activeItem ) {
				activeItem.classList.add( 'is-active' );
			}
		}

		prevBtn.addEventListener( 'click', function () {
			if ( pdfDoc ) {
				renderPage( currentPage - 1 );
			}
		} );

		nextBtn.addEventListener( 'click', function () {
			if ( pdfDoc ) {
				renderPage( currentPage + 1 );
			}
		} );

		pageInput.addEventListener( 'change', function () {
			var num = parseInt( pageInput.value, 10 );
			if ( pdfDoc && ! isNaN( num ) ) {
				renderPage( num );
			}
		} );

		zoomInBtn.addEventListener( 'click', function () {
			zoomFactor = Math.min( 3, zoomFactor + 0.25 );
			if ( pdfDoc ) {
				renderPage( currentPage );
			}
		} );

		zoomOutBtn.addEventListener( 'click', function () {
			zoomFactor = Math.max( 0.5, zoomFactor - 0.25 );
			if ( pdfDoc ) {
				renderPage( currentPage );
			}
		} );

		tocLinks.forEach( function ( link ) {
			link.addEventListener( 'click', function () {
				var page = parseInt( link.getAttribute( 'data-page' ), 10 );
				if ( pdfDoc && ! isNaN( page ) ) {
					renderPage( page );
				}
				if ( sidebar && sidebar.classList.contains( 'is-open' ) && window.innerWidth <= 860 ) {
					sidebar.classList.remove( 'is-open' );
				}
			} );
		} );

		document.addEventListener( 'keydown', function ( event ) {
			var tag = ( event.target && event.target.tagName ) || '';
			if ( 'INPUT' === tag || 'TEXTAREA' === tag ) {
				return;
			}
			if ( ! pdfDoc || ! isInViewport( layout ) ) {
				return;
			}
			if ( 'ArrowRight' === event.key ) {
				renderPage( currentPage + 1 );
			} else if ( 'ArrowLeft' === event.key ) {
				renderPage( currentPage - 1 );
			}
		} );

		var resizeTimer = null;
		window.addEventListener( 'resize', function () {
			clearTimeout( resizeTimer );
			resizeTimer = setTimeout( function () {
				baseWidth = null;
				if ( pdfDoc ) {
					renderPage( currentPage );
				}
			}, 250 );
		} );
	}

	function isInViewport( el ) {
		var rect = el.getBoundingClientRect();
		return rect.bottom > 0 && rect.top < ( window.innerHeight || document.documentElement.clientHeight );
	}
} )();
