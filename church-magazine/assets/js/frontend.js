( function () {
	'use strict';

	document.addEventListener( 'DOMContentLoaded', function () {
		var magazines = document.querySelectorAll( '.cm-magazine' );
		if ( ! magazines.length ) {
			return;
		}

		var smoothScroll = ! window.ChurchMagazine || window.ChurchMagazine.smoothScroll !== false;

		magazines.forEach( function ( magazine ) {
			setUpToggle( magazine );
			setUpTocLinks( magazine, smoothScroll );
			setUpActiveHighlighting( magazine );
		} );
	} );

	function setUpToggle( magazine ) {
		var toggle = magazine.querySelector( '.cm-toc-toggle' );
		var sidebar = magazine.querySelector( '.cm-sidebar' );
		if ( ! toggle || ! sidebar ) {
			return;
		}

		toggle.addEventListener( 'click', function () {
			var isOpen = sidebar.classList.toggle( 'is-open' );
			toggle.setAttribute( 'aria-expanded', isOpen ? 'true' : 'false' );
		} );
	}

	function setUpTocLinks( magazine, smoothScroll ) {
		var links = magazine.querySelectorAll( '.cm-toc-item a' );
		var sidebar = magazine.querySelector( '.cm-sidebar' );

		links.forEach( function ( link ) {
			link.addEventListener( 'click', function ( event ) {
				var targetId = link.getAttribute( 'data-target' );
				var target = targetId ? document.getElementById( targetId ) : null;

				if ( target && smoothScroll ) {
					event.preventDefault();
					target.scrollIntoView( { behavior: 'smooth', block: 'start' } );
					history.pushState( null, '', '#' + targetId );
				}

				if ( sidebar && sidebar.classList.contains( 'is-open' ) && window.innerWidth <= 860 ) {
					sidebar.classList.remove( 'is-open' );
				}
			} );
		} );
	}

	function setUpActiveHighlighting( magazine ) {
		var sections = magazine.querySelectorAll( '.cm-article' );
		var items = magazine.querySelectorAll( '.cm-toc-item' );
		if ( ! sections.length || ! items.length || ! ( 'IntersectionObserver' in window ) ) {
			return;
		}

		var itemById = {};
		items.forEach( function ( item ) {
			var link = item.querySelector( 'a[data-target]' );
			if ( link ) {
				itemById[ link.getAttribute( 'data-target' ) ] = item;
			}
		} );

		var observer = new IntersectionObserver(
			function ( entries ) {
				entries.forEach( function ( entry ) {
					var item = itemById[ entry.target.id ];
					if ( ! item ) {
						return;
					}
					if ( entry.isIntersecting ) {
						items.forEach( function ( i ) {
							i.classList.remove( 'is-active' );
						} );
						item.classList.add( 'is-active' );
					}
				} );
			},
			{ rootMargin: '-20% 0px -70% 0px', threshold: 0 }
		);

		sections.forEach( function ( section ) {
			observer.observe( section );
		} );
	}
} )();
