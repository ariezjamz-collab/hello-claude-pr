jQuery( function ( $ ) {
	'use strict';

	var $table = $( '#cm_toc_table' );
	if ( ! $table.length ) {
		return;
	}

	var $body = $table.find( 'tbody' );

	function newRow() {
		var $row = $body.find( '.cm-toc-row' ).first().clone();
		$row.find( 'input[type="text"]' ).val( '' );
		$row.find( 'input[type="number"]' ).val( '' );
		return $row;
	}

	$( '#cm_toc_add_row' ).on( 'click', function () {
		$body.append( newRow() );
	} );

	$body.on( 'click', '.cm-toc-remove-row', function () {
		if ( $body.find( '.cm-toc-row' ).length > 1 ) {
			$( this ).closest( '.cm-toc-row' ).remove();
		} else {
			$( this ).closest( '.cm-toc-row' ).find( 'input' ).val( '' );
		}
	} );

	$body.on( 'click', '.cm-toc-move-up', function () {
		var $row = $( this ).closest( '.cm-toc-row' );
		var $prev = $row.prev( '.cm-toc-row' );
		if ( $prev.length ) {
			$row.insertBefore( $prev );
		}
	} );

	$body.on( 'click', '.cm-toc-move-down', function () {
		var $row = $( this ).closest( '.cm-toc-row' );
		var $next = $row.next( '.cm-toc-row' );
		if ( $next.length ) {
			$row.insertAfter( $next );
		}
	} );
} );
